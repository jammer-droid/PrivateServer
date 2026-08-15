#pragma once

#include "WorldClock.h"
#include "WorldIngressDoubleBuffer.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace psnr::world
{
    enum class WorldIngressPumpStepStopReason : std::uint8_t
    {
        Progressed = 0,
        Idle,
        SlotFull,
        SourceClosed,
        SourceFailure,
        BufferFailure,
        InvalidArgument,
    };

    struct WorldIngressPumpStepReport final
    {
        WorldIngressPumpStepStopReason stopReason = WorldIngressPumpStepStopReason::Idle;
        std::size_t drainedEventCount = 0;
        psnr::core::NrStatus sourceStatus{};
    };

    struct WorldIngressPumpMetrics final
    {
        std::uint64_t drainedEventCount = 0;
        std::uint64_t drainBatchCount = 0;
        std::uint64_t slotEventHighWatermark = 0;
        std::uint64_t slotFullEpochCount = 0;
    };

    enum class WorldIngressTerminalEpochStopReason : std::uint8_t
    {
        SealedFull = 0,
        SourceClosed,
        TimedOut,
        SourceFailure,
        ExchangeBusy,
        InvalidArgument,
    };

    struct WorldIngressTerminalEpochReport final
    {
        WorldIngressTerminalEpochStopReason stopReason = WorldIngressTerminalEpochStopReason::SealedFull;
        std::uint64_t epoch = 0;
        std::size_t drainedEventCount = 0;
        std::size_t drainBatchCount = 0;
        psnr::core::NrStatus sourceStatus{};
    };

    // Runtime queue에서 bounded batch를 현재 write claim의 payload로 직접 이동한다.
    // source wait는 claim을 반납한 상태에서 수행하며 swap과 epoch 결정은 Coordinator 책임이다.
    class WorldIngressPump final
    {
    public:
        using Clock = WorldClock;

        explicit WorldIngressPump(WorldIngressDoubleBuffer& buffer) noexcept
            : buffer_(buffer)
        {
        }

        template <typename TEventSource>
        [[nodiscard]] WorldIngressPumpStepReport RunStep(TEventSource& source,
                                                         const std::chrono::milliseconds waitTimeout)
        {
            if (waitTimeout.count() < 0)
            {
                return MakeStepReport(WorldIngressPumpStepStopReason::InvalidArgument, 0,
                                      psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument));
            }

            WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError> writeResult =
                buffer_.WaitAcquireWrite(minimumWriteGeneration_, waitTimeout);
            if (writeResult.Failed())
            {
                if (writeResult.Error() == WorldDoubleBufferRoleExchangeError::TimedOut)
                {
                    return MakeStepReport(WorldIngressPumpStepStopReason::Idle, 0, psnr::core::NrStatus::Success());
                }
                return MakeStepReport(WorldIngressPumpStepStopReason::BufferFailure, 0,
                                      psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState));
            }

            WorldIngressWriteBatch writeBatch = writeResult.TakeValue();
            writeSlotFull_ = false;
            writeSlotFullReported_ = false;
            if (writeBatch.events.empty())
            {
                minimumWriteGeneration_ = writeBatch.claim.generation + 1;
                writeSlotFull_ = true;
                if (buffer_.CommitWrite(writeBatch, 0).Failed())
                {
                    return MakeStepReport(WorldIngressPumpStepStopReason::BufferFailure, 0,
                                          psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState));
                }
                ++metrics_.slotFullEpochCount;
                return MakeStepReport(WorldIngressPumpStepStopReason::SlotFull, 0, psnr::core::NrStatus::Success());
            }

            const std::size_t requestedEventCount = writeBatch.events.size() < psnr::runtime::NrMaxToWorldEventBatchSize
                                                        ? writeBatch.events.size()
                                                        : psnr::runtime::NrMaxToWorldEventBatchSize;
            std::size_t eventCount = 0;
            const psnr::core::NrStatus popStatus =
                source.TryPopBatch(writeBatch.events.data(), requestedEventCount, &eventCount);
            if (popStatus.Succeeded())
            {
                if (eventCount == 0 || eventCount > requestedEventCount ||
                    buffer_.CommitWrite(writeBatch, eventCount).Failed())
                {
                    static_cast<void>(buffer_.CommitWrite(writeBatch, 0));
                    return MakeStepReport(WorldIngressPumpStepStopReason::SourceFailure, 0,
                                          psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState));
                }

                metrics_.drainedEventCount += eventCount;
                ++metrics_.drainBatchCount;
                const std::uint64_t slotEventCount =
                    static_cast<std::uint64_t>(buffer_.EventCapacityPerSlot() - writeBatch.events.size() + eventCount);
                if (slotEventCount > metrics_.slotEventHighWatermark)
                {
                    metrics_.slotEventHighWatermark = slotEventCount;
                }
                return MakeStepReport(WorldIngressPumpStepStopReason::Progressed, eventCount,
                                      psnr::core::NrStatus::Success());
            }

            if (buffer_.CommitWrite(writeBatch, 0).Failed())
            {
                return MakeStepReport(WorldIngressPumpStepStopReason::BufferFailure, 0,
                                      psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState));
            }
            if (popStatus.ErrorCode() != psnr::core::NrErrorCode::QueueEmpty)
            {
                return MakeStepReport(WorldIngressPumpStepStopReason::SourceFailure, 0, popStatus);
            }

            psnr::runtime::NrToWorldWaitResult waitResult = psnr::runtime::NrToWorldWaitResult::TimedOut;
            const psnr::core::NrStatus waitStatus =
                source.WaitForEvents(CalculateWorldWaitTimeoutMilliseconds(waitTimeout), &waitResult);
            if (waitStatus.Failed())
            {
                return MakeStepReport(WorldIngressPumpStepStopReason::SourceFailure, 0, waitStatus);
            }
            if (waitResult == psnr::runtime::NrToWorldWaitResult::Closed)
            {
                return MakeStepReport(WorldIngressPumpStepStopReason::SourceClosed, 0, psnr::core::NrStatus::Success());
            }
            if (waitResult != psnr::runtime::NrToWorldWaitResult::EventsAvailable &&
                waitResult != psnr::runtime::NrToWorldWaitResult::TimedOut)
            {
                return MakeStepReport(WorldIngressPumpStepStopReason::SourceFailure, 0,
                                      psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState));
            }
            return MakeStepReport(WorldIngressPumpStepStopReason::Idle, 0, psnr::core::NrStatus::Success());
        }

        // terminal에서는 slot full 또는 source close까지 step을 반복한다. 실제 swap은 Coordinator가 수행한다.
        template <typename TEventSource, typename TClockSource>
        [[nodiscard]] WorldIngressTerminalEpochReport RunTerminalEpoch(TEventSource& source, TClockSource& clockSource,
                                                                       const std::uint64_t epoch,
                                                                       const Clock::time_point terminalDeadline)
        {
            if (epoch == 0)
            {
                return MakeTerminalReport(WorldIngressTerminalEpochStopReason::InvalidArgument, epoch, 0, 0,
                                          psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument));
            }
            if (writeSlotFull_ && !writeSlotFullReported_)
            {
                writeSlotFullReported_ = true;
                return MakeTerminalReport(WorldIngressTerminalEpochStopReason::SealedFull, epoch, 0, 0,
                                          psnr::core::NrStatus::Success());
            }

            std::size_t drainedEventCount = 0;
            std::size_t drainBatchCount = 0;
            while (clockSource.Now() < terminalDeadline)
            {
                const std::chrono::milliseconds remainingWait =
                    std::chrono::duration_cast<std::chrono::milliseconds>(terminalDeadline - clockSource.Now());
                const WorldIngressPumpStepReport stepReport = RunStep(source, remainingWait);
                drainedEventCount += stepReport.drainedEventCount;
                if (stepReport.stopReason == WorldIngressPumpStepStopReason::Progressed)
                {
                    ++drainBatchCount;
                    continue;
                }
                if (stepReport.stopReason == WorldIngressPumpStepStopReason::Idle)
                {
                    continue;
                }
                if (stepReport.stopReason == WorldIngressPumpStepStopReason::SlotFull)
                {
                    writeSlotFullReported_ = true;
                    return MakeTerminalReport(WorldIngressTerminalEpochStopReason::SealedFull, epoch, drainedEventCount,
                                              drainBatchCount, psnr::core::NrStatus::Success());
                }
                if (stepReport.stopReason == WorldIngressPumpStepStopReason::SourceClosed)
                {
                    return MakeTerminalReport(WorldIngressTerminalEpochStopReason::SourceClosed, epoch,
                                              drainedEventCount, drainBatchCount, psnr::core::NrStatus::Success());
                }
                return MakeTerminalReport(WorldIngressTerminalEpochStopReason::SourceFailure, epoch, drainedEventCount,
                                          drainBatchCount, stepReport.sourceStatus);
            }

            return MakeTerminalReport(WorldIngressTerminalEpochStopReason::TimedOut, epoch, drainedEventCount,
                                      drainBatchCount, psnr::core::NrStatus::Success());
        }

        [[nodiscard]] WorldIngressPumpMetrics Metrics() const noexcept
        {
            return metrics_;
        }

        void Close() noexcept
        {
            buffer_.Close();
        }

    private:
        [[nodiscard]] static WorldIngressPumpStepReport MakeStepReport(const WorldIngressPumpStepStopReason stopReason,
                                                                       const std::size_t drainedEventCount,
                                                                       const psnr::core::NrStatus sourceStatus) noexcept
        {
            return WorldIngressPumpStepReport{stopReason, drainedEventCount, sourceStatus};
        }

        [[nodiscard]] static WorldIngressTerminalEpochReport MakeTerminalReport(
            const WorldIngressTerminalEpochStopReason stopReason, const std::uint64_t epoch,
            const std::size_t drainedEventCount, const std::size_t drainBatchCount,
            const psnr::core::NrStatus sourceStatus) noexcept
        {
            return WorldIngressTerminalEpochReport{
                stopReason, epoch, drainedEventCount, drainBatchCount, sourceStatus,
            };
        }

        WorldIngressDoubleBuffer& buffer_;
        std::uint64_t minimumWriteGeneration_ = 1;
        bool writeSlotFull_ = false;
        bool writeSlotFullReported_ = false;
        WorldIngressPumpMetrics metrics_;
    };
} // namespace psnr::world
