#include "pch.h"

#include "WorldIngressPump.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        class FakePumpClock final
        {
        public:
            using Clock = WorldIngressPump::Clock;

            [[nodiscard]] Clock::time_point Now() const noexcept
            {
                return now_;
            }

            void Advance(const std::chrono::milliseconds duration) noexcept
            {
                now_ += duration;
            }

        private:
            Clock::time_point now_{};
        };

        struct WaitAction final
        {
            psnr::runtime::NrToWorldWaitResult result = psnr::runtime::NrToWorldWaitResult::TimedOut;
            std::chrono::milliseconds elapsed{};
            std::size_t addedEventCount = 0;
        };

        class ScriptedPumpSource final
        {
        public:
            ScriptedPumpSource(FakePumpClock& clock, const std::size_t initialEventCount,
                               std::vector<WaitAction> waitActions,
                               const psnr::core::NrStatus emptyStatus =
                                   psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty))
                : clock_(clock)
                , availableEventCount_(initialEventCount)
                , waitActions_(std::move(waitActions))
                , emptyStatus_(emptyStatus)
            {
            }

            [[nodiscard]] psnr::core::NrStatus TryPopBatch(psnr::runtime::NrToWorldEvent* const eventBuffer,
                                                           const std::size_t eventBufferCount,
                                                           std::size_t* const outEventCount) noexcept
            {
                ++popCallCount_;
                if (availableEventCount_ == 0)
                {
                    return emptyStatus_;
                }

                const std::size_t eventCount = std::min(availableEventCount_, eventBufferCount);
                for (std::size_t index = 0; index < eventCount; ++index)
                {
                    eventBuffer[index] = psnr::runtime::NrToWorldEvent{};
                }
                availableEventCount_ -= eventCount;
                *outEventCount = eventCount;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus WaitForEvents(
                const std::uint32_t timeoutMilliseconds,
                psnr::runtime::NrToWorldWaitResult* const outWaitResult) noexcept
            {
                waitTimeouts_.push_back(timeoutMilliseconds);
                if (nextWaitActionIndex_ >= waitActions_.size())
                {
                    clock_.Advance(std::chrono::milliseconds{timeoutMilliseconds});
                    *outWaitResult = psnr::runtime::NrToWorldWaitResult::TimedOut;
                    return psnr::core::NrStatus::Success();
                }

                const WaitAction& action = waitActions_[nextWaitActionIndex_];
                ++nextWaitActionIndex_;
                clock_.Advance(action.elapsed);
                availableEventCount_ += action.addedEventCount;
                *outWaitResult = action.result;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] std::size_t AvailableEventCount() const noexcept
            {
                return availableEventCount_;
            }

            [[nodiscard]] std::size_t PopCallCount() const noexcept
            {
                return popCallCount_;
            }

            [[nodiscard]] const std::vector<std::uint32_t>& WaitTimeouts() const noexcept
            {
                return waitTimeouts_;
            }

        private:
            FakePumpClock& clock_;
            std::size_t availableEventCount_ = 0;
            std::size_t popCallCount_ = 0;
            std::vector<WaitAction> waitActions_;
            std::size_t nextWaitActionIndex_ = 0;
            std::vector<std::uint32_t> waitTimeouts_;
            psnr::core::NrStatus emptyStatus_{};
        };

        [[nodiscard]] std::unique_ptr<WorldIngressDoubleBuffer> CreateBuffer(const std::size_t eventCapacityPerSlot)
        {
            WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> result =
                WorldIngressDoubleBuffer::Create(eventCapacityPerSlot);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] WorldIngressReadBatch SwapAndAcquire(WorldIngressDoubleBuffer& buffer, const std::uint64_t epoch)
        {
            EXPECT_TRUE(buffer.WaitSwap(epoch, std::chrono::milliseconds{100}).Succeeded());
            WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError> readResult =
                buffer.WaitAcquireRead(epoch, std::chrono::milliseconds{100});
            EXPECT_TRUE(readResult.Succeeded());
            return readResult.Failed() ? WorldIngressReadBatch{} : readResult.TakeValue();
        }
    } // namespace

    TEST(WorldIngressDoubleBufferTests, RejectsInvalidCreationArguments)
    {
        const WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> result = WorldIngressDoubleBuffer::Create(0);

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidArgument);
    }

    TEST(WorldIngressDoubleBufferTests, AlternatesClaimedSlotsWhileReadAndWriteProceedTogether)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(3);
        ASSERT_NE(buffer, nullptr);

        WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError> firstWriteResult =
            buffer->WaitAcquireWrite(1, std::chrono::milliseconds{100});
        ASSERT_TRUE(firstWriteResult.Succeeded());
        WorldIngressWriteBatch firstWrite = firstWriteResult.TakeValue();
        EXPECT_EQ(firstWrite.events.size(), 3u);
        ASSERT_TRUE(buffer->CommitWrite(firstWrite, 2).Succeeded());

        WorldIngressReadBatch firstRead = SwapAndAcquire(*buffer, 1);
        ASSERT_EQ(firstRead.events.size(), 2u);

        WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError> secondWriteResult =
            buffer->WaitAcquireWrite(2, std::chrono::milliseconds{100});
        ASSERT_TRUE(secondWriteResult.Succeeded());
        WorldIngressWriteBatch secondWrite = secondWriteResult.TakeValue();
        EXPECT_NE(firstRead.claim.slotIndex, secondWrite.claim.slotIndex);
        ASSERT_TRUE(buffer->CommitWrite(secondWrite, 1).Succeeded());

        EXPECT_TRUE(buffer->WaitSwap(2, std::chrono::milliseconds{0}).Failed());
        ASSERT_TRUE(buffer->ReleaseRead(firstRead).Succeeded());
        WorldIngressReadBatch secondRead = SwapAndAcquire(*buffer, 2);
        EXPECT_EQ(secondRead.events.size(), 1u);
        EXPECT_TRUE(buffer->ReleaseRead(secondRead).Succeeded());
    }

    TEST(WorldIngressPumpTests, MovesBoundedBatchesDirectlyIntoCurrentWriteSlot)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(200);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{clock, 130, {}};

        const WorldIngressPumpStepReport first = pump.RunStep(source, std::chrono::milliseconds{10});
        const WorldIngressPumpStepReport second = pump.RunStep(source, std::chrono::milliseconds{10});

        EXPECT_EQ(first.stopReason, WorldIngressPumpStepStopReason::Progressed);
        EXPECT_EQ(first.drainedEventCount, psnr::runtime::NrMaxToWorldEventBatchSize);
        EXPECT_EQ(second.stopReason, WorldIngressPumpStepStopReason::Progressed);
        EXPECT_EQ(second.drainedEventCount, 2u);
        EXPECT_EQ(source.AvailableEventCount(), 0u);

        WorldIngressReadBatch readBatch = SwapAndAcquire(*buffer, 1);
        EXPECT_EQ(readBatch.events.size(), 130u);
        EXPECT_TRUE(buffer->ReleaseRead(readBatch).Succeeded());
    }

    TEST(WorldIngressPumpTests, QueueWaitOccursAfterWriteClaimRelease)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{
            clock,
            0,
            {{psnr::runtime::NrToWorldWaitResult::TimedOut, std::chrono::milliseconds{10}, 0}},
        };

        const WorldIngressPumpStepReport report = pump.RunStep(source, std::chrono::milliseconds{10});

        EXPECT_EQ(report.stopReason, WorldIngressPumpStepStopReason::Idle);
        EXPECT_EQ(source.WaitTimeouts(), std::vector<std::uint32_t>{10});
        EXPECT_TRUE(buffer->WaitSwap(1, std::chrono::milliseconds{0}).Succeeded());
        WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError> readResult =
            buffer->WaitAcquireRead(1, std::chrono::milliseconds{0});
        ASSERT_TRUE(readResult.Succeeded());
        WorldIngressReadBatch readBatch = readResult.TakeValue();
        EXPECT_TRUE(readBatch.events.empty());
        EXPECT_TRUE(buffer->ReleaseRead(readBatch).Succeeded());
    }

    TEST(WorldIngressPumpTests, FullSlotLeavesQueueBacklogUntilCoordinatorSwap)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{clock, 5, {}};

        EXPECT_EQ(pump.RunStep(source, std::chrono::milliseconds{10}).stopReason,
                  WorldIngressPumpStepStopReason::Progressed);
        EXPECT_EQ(pump.RunStep(source, std::chrono::milliseconds{0}).stopReason,
                  WorldIngressPumpStepStopReason::SlotFull);
        EXPECT_EQ(source.AvailableEventCount(), 3u);
        EXPECT_EQ(source.PopCallCount(), 1u);

        WorldIngressReadBatch readBatch = SwapAndAcquire(*buffer, 1);
        EXPECT_EQ(readBatch.events.size(), 2u);

        EXPECT_EQ(pump.RunStep(source, std::chrono::milliseconds{10}).stopReason,
                  WorldIngressPumpStepStopReason::Progressed);
        EXPECT_EQ(source.AvailableEventCount(), 1u);
        EXPECT_TRUE(buffer->ReleaseRead(readBatch).Succeeded());

        const WorldIngressPumpMetrics metrics = pump.Metrics();
        EXPECT_EQ(metrics.slotEventHighWatermark, 2u);
        EXPECT_EQ(metrics.slotFullEpochCount, 1u);
    }

    TEST(WorldIngressPumpTests, NextWriteProgressesWhilePreviousReadClaimRemainsActive)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{clock, 2, {}};

        WorldIngressReadBatch previousRead = SwapAndAcquire(*buffer, 1);
        std::future<WorldIngressPumpStepReport> writeResult = std::async(
            std::launch::async, [&pump, &source]() { return pump.RunStep(source, std::chrono::milliseconds{100}); });

        ASSERT_EQ(writeResult.wait_for(std::chrono::milliseconds{50}), std::future_status::ready);
        const WorldIngressPumpStepReport report = writeResult.get();
        EXPECT_EQ(report.stopReason, WorldIngressPumpStepStopReason::Progressed);
        EXPECT_EQ(report.drainedEventCount, 2u);
        EXPECT_TRUE(buffer->ReleaseRead(previousRead).Succeeded());

        WorldIngressReadBatch nextRead = SwapAndAcquire(*buffer, 2);
        EXPECT_EQ(nextRead.events.size(), 2u);
        EXPECT_TRUE(buffer->ReleaseRead(nextRead).Succeeded());
    }

    TEST(WorldIngressPumpTests, SourceFailureDoesNotPublishCurrentWriteSlot)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{
            clock,
            0,
            {},
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 123),
        };

        const WorldIngressPumpStepReport report = pump.RunStep(source, std::chrono::milliseconds{10});

        EXPECT_EQ(report.stopReason, WorldIngressPumpStepStopReason::SourceFailure);
        EXPECT_EQ(report.sourceStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        EXPECT_EQ(report.sourceStatus.NativeErrorCode(), 123u);
        EXPECT_TRUE(buffer->WaitAcquireRead(1, std::chrono::milliseconds{0}).Failed());
    }

    TEST(WorldIngressPumpTests, TerminalEpochReportsFullAndCoordinatorPublishesSlot)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{clock, 5, {}};

        const WorldIngressTerminalEpochReport report =
            pump.RunTerminalEpoch(source, clock, 1, clock.Now() + std::chrono::milliseconds{100});

        EXPECT_EQ(report.stopReason, WorldIngressTerminalEpochStopReason::SealedFull);
        EXPECT_EQ(report.drainedEventCount, 2u);
        EXPECT_EQ(source.AvailableEventCount(), 3u);
        WorldIngressReadBatch readBatch = SwapAndAcquire(*buffer, 1);
        EXPECT_EQ(readBatch.events.size(), 2u);
        EXPECT_TRUE(buffer->ReleaseRead(readBatch).Succeeded());
    }

    TEST(WorldIngressPumpTests, TerminalEpochReportsSourceCloseAndCoordinatorPublishesEmptyMarker)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{
            clock,
            0,
            {{psnr::runtime::NrToWorldWaitResult::Closed, std::chrono::milliseconds{0}, 0}},
        };

        const WorldIngressTerminalEpochReport report =
            pump.RunTerminalEpoch(source, clock, 7, clock.Now() + std::chrono::milliseconds{100});

        EXPECT_EQ(report.stopReason, WorldIngressTerminalEpochStopReason::SourceClosed);
        WorldIngressReadBatch readBatch = SwapAndAcquire(*buffer, 7);
        EXPECT_TRUE(readBatch.events.empty());
        EXPECT_TRUE(buffer->ReleaseRead(readBatch).Succeeded());
    }

    TEST(WorldIngressPumpTests, TerminalDrainContinuesOnNewWriteSlotAfterReportedFullSlotSwaps)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateBuffer(2);
        ASSERT_NE(buffer, nullptr);
        WorldIngressPump pump{*buffer};
        FakePumpClock clock;
        ScriptedPumpSource source{
            clock,
            3,
            {{psnr::runtime::NrToWorldWaitResult::Closed, std::chrono::milliseconds{0}, 0}},
        };

        const WorldIngressTerminalEpochReport firstReport =
            pump.RunTerminalEpoch(source, clock, 1, clock.Now() + std::chrono::milliseconds{100});
        ASSERT_EQ(firstReport.stopReason, WorldIngressTerminalEpochStopReason::SealedFull);
        WorldIngressReadBatch firstRead = SwapAndAcquire(*buffer, 1);
        ASSERT_EQ(firstRead.events.size(), 2u);
        ASSERT_TRUE(buffer->ReleaseRead(firstRead).Succeeded());

        const WorldIngressTerminalEpochReport secondReport =
            pump.RunTerminalEpoch(source, clock, 2, clock.Now() + std::chrono::milliseconds{100});
        EXPECT_EQ(secondReport.stopReason, WorldIngressTerminalEpochStopReason::SourceClosed);
        EXPECT_EQ(secondReport.drainedEventCount, 1u);
        WorldIngressReadBatch secondRead = SwapAndAcquire(*buffer, 2);
        EXPECT_EQ(secondRead.events.size(), 1u);
        EXPECT_TRUE(buffer->ReleaseRead(secondRead).Succeeded());
    }
} // namespace psnr::world::tests
