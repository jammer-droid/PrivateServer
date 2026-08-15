#pragma once

#include "WorldClock.h"
#include "WorldFixedStepSchedule.h"
#include "WorldIngressEventConsumer.h"
#include "WorldIngressEventDrain.h"
#include "WorldSessionRegistry.h"
#include "WorldTickProcessor.h"

#include <PrivateServer/NetworkRuntime/NrServer.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace psnr::world
{
    enum class WorldOwnerIterationStopReason : std::uint8_t
    {
        Completed = 0,
        TickNotDue,
        IngressSourceFailure,
        ScheduleFailure,
        TickProcessFailure,
    };

    enum class WorldOwnerWakeReason : std::uint8_t
    {
        EventsAvailable = 0,  // Runtime 의 ToWorld queue 에 이벤트 생김
        TickDeadline,         // 다음 tick 시간이 이미 도래했거나, event 를 기다리던 도중 timeout 에 도달
        IngressClosed,        // Runtime Producer 가 종료
        IngressSourceFailure, // Runtime Wait 호출 자체 실패
    };

    struct WorldOwnerWaitReport final
    {
        WorldOwnerWakeReason wakeReason = WorldOwnerWakeReason::TickDeadline;
        std::uint32_t waitTimeoutMilliseconds = 0;
        psnr::core::NrStatus sourceStatus{};
    };

    enum class WorldOwnerStepStopReason : std::uint8_t
    {
        IterationCompleted = 0,
        IngressClosed,
        WaitFailed,
        IterationFailed,
    };

    struct WorldOwnerIterationReport final
    {
        WorldOwnerIterationStopReason stopReason = WorldOwnerIterationStopReason::Completed;
        WorldIngressDrainReport ingressDrain{};
        WorldControlledStatePublishReport controlledStatePublish{};
        WorldFixedStepTakeResult scheduleTakeResult = WorldFixedStepTakeResult::NotDue;
        WorldTickProcessResult tickProcessResult = WorldTickProcessResult::Processed;
        std::uint32_t processedTickCount = 0;
        std::uint32_t lastCompletedServerTick = 0;
        bool overrun = false;
    };

    struct WorldOwnerStepReport final
    {
        WorldOwnerStepStopReason stopReason = WorldOwnerStepStopReason::IterationCompleted;
        WorldOwnerWaitReport wait{};
        WorldOwnerIterationReport iteration{};
        bool iterationRan = false;
    };

    struct WorldOwnerMetrics final
    {
        std::uint64_t drainedEventCount = 0;
        std::uint64_t drainBudgetExhaustionCount = 0;
        std::uint64_t processedTickCount = 0;
        std::uint64_t overrunBatchCount = 0;
        std::uint64_t tickProcessFailureCount = 0;
        std::uint64_t processedCommandCount = 0;
        std::uint64_t maximumCommandsPerTick = 0;
        std::uint64_t maximumTickStartLagMicroseconds = 0;
        std::uint64_t maximumTickDurationNanoseconds = 0;
    };

    // 한 번의 World owner 실행 기회에서 ingress를 먼저 제한적으로 비운 뒤 due tick batch 전체를 처리한다.
    // wait는 다음 tick deadline까지만 수행한다. thread 생성과 shutdown orchestration은 외부 조립 계층 책임이다.
    // 1. WaitForNextIteration
    //      - event 도착 또는 tick deadline 까지 대기
    // 2. RunIteration
    //      - ingress batch drain
    //      - due tick batch 처리
    class WorldOwnerLoop final
    {
    public:
        WorldOwnerLoop(WorldFixedStepSchedule schedule, WorldSessionRegistry& sessionRegistry,
                       WorldMovementCommandStore& movementCommandStore, WorldEntityManager& entityManager,
                       const std::size_t maxIngressEventCount, const std::uint32_t initialLastCompletedServerTick,
                       const WorldTickProcessorConfig& tickProcessorConfig = {})
            : schedule_(std::move(schedule))
            , sessionRegistry_(sessionRegistry)
            , movementCommandStore_(movementCommandStore)
            , entityManager_(entityManager)
            , tickProcessor_(tickProcessorConfig)
            , maxIngressEventCount_(maxIngressEventCount)
            , lastCompletedServerTick_(initialLastCompletedServerTick)
        {
            assert(schedule_.IsValid());
            assert(maxIngressEventCount_ > 0);
        }

        // fixedDeltaTime 사이에 비는 시간이 있으면 wait
        template <typename TEventSource>
        [[nodiscard]] WorldOwnerWaitReport WaitForNextIteration(TEventSource& eventSource,
                                                                const WorldFixedStepSchedule::Clock::time_point now)
        {
            const WorldFixedStepSchedule::Clock::time_point nextDeadline = schedule_.NextDeadline();
            if (now >= nextDeadline) // 다음 tick 실행 시간이 이미 지난 경우, 즉시 due tick 확인 필요
            {
                return WorldOwnerWaitReport{
                    WorldOwnerWakeReason::TickDeadline,
                    0,
                    psnr::core::NrStatus::Success(),
                };
            }

            const std::uint32_t waitTimeoutMilliseconds = CalculateWorldWaitTimeoutMilliseconds(nextDeadline - now);
            psnr::runtime::NrToWorldWaitResult sourceWaitResult = psnr::runtime::NrToWorldWaitResult::TimedOut;
            const psnr::core::NrStatus waitStatus =
                eventSource.WaitForEvents(waitTimeoutMilliseconds, &sourceWaitResult);
            if (waitStatus.Failed())
            {
                return WorldOwnerWaitReport{
                    WorldOwnerWakeReason::IngressSourceFailure,
                    waitTimeoutMilliseconds,
                    waitStatus,
                };
            }

            switch (sourceWaitResult)
            {
            case psnr::runtime::NrToWorldWaitResult::EventsAvailable:
                return WorldOwnerWaitReport{
                    WorldOwnerWakeReason::EventsAvailable,
                    waitTimeoutMilliseconds,
                    waitStatus,
                };
            case psnr::runtime::NrToWorldWaitResult::TimedOut:
                return WorldOwnerWaitReport{
                    WorldOwnerWakeReason::TickDeadline,
                    waitTimeoutMilliseconds,
                    waitStatus,
                };
            case psnr::runtime::NrToWorldWaitResult::Closed:
                return WorldOwnerWaitReport{
                    WorldOwnerWakeReason::IngressClosed,
                    waitTimeoutMilliseconds,
                    waitStatus,
                };
            }

            return WorldOwnerWaitReport{
                WorldOwnerWakeReason::IngressSourceFailure,
                waitTimeoutMilliseconds,
                psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState),
            };
        }

        template <typename TEventSource, typename TEventConsumer, typename TClockSource>
        [[nodiscard]] WorldOwnerStepReport RunNext(TEventSource& eventSource, TEventConsumer& eventConsumer,
                                                   TClockSource& clockSource)
        {
            const WorldFixedStepSchedule::Clock::time_point beforeWait = clockSource.Now();
            const WorldOwnerWaitReport wait = WaitForNextIteration(eventSource, beforeWait);
            if (wait.wakeReason == WorldOwnerWakeReason::IngressClosed)
            {
                return WorldOwnerStepReport{
                    WorldOwnerStepStopReason::IngressClosed,
                    wait,
                    WorldOwnerIterationReport{},
                    false,
                };
            }
            if (wait.wakeReason == WorldOwnerWakeReason::IngressSourceFailure)
            {
                return WorldOwnerStepReport{
                    WorldOwnerStepStopReason::WaitFailed,
                    wait,
                    WorldOwnerIterationReport{},
                    false,
                };
            }

            const WorldFixedStepSchedule::Clock::time_point afterWait = clockSource.Now();
            const WorldOwnerIterationReport iteration = RunIteration(eventSource, eventConsumer, afterWait);
            const bool iterationCompleted = iteration.stopReason == WorldOwnerIterationStopReason::Completed ||
                                            iteration.stopReason == WorldOwnerIterationStopReason::TickNotDue;
            return WorldOwnerStepReport{
                iterationCompleted ? WorldOwnerStepStopReason::IterationCompleted
                                   : WorldOwnerStepStopReason::IterationFailed,
                wait,
                iteration,
                true,
            };
        }

        template <typename TEventSource, typename TEventConsumer>
        [[nodiscard]] WorldOwnerIterationReport RunIteration(TEventSource& eventSource, TEventConsumer& eventConsumer,
                                                             const WorldFixedStepSchedule::Clock::time_point now)
        {
            constexpr std::uint64_t MaximumServerTick = std::numeric_limits<std::uint32_t>::max();
            const std::uint64_t nextServerTick = schedule_.NextServerTick();
            if (!schedule_.IsValid() || maxIngressEventCount_ == 0 || nextServerTick > MaximumServerTick)
            {
                return MakeReport(WorldOwnerIterationStopReason::ScheduleFailure, WorldIngressDrainReport{},
                                  WorldFixedStepTakeResult::ServerTickOverflow, WorldTickProcessResult::Processed, 0,
                                  false);
            }

            eventConsumer.UpdateTickContext(static_cast<std::uint32_t>(nextServerTick), lastCompletedServerTick_);
            const WorldIngressDrainReport ingressDrain =
                WorldIngressEventDrain::Drain(eventSource, eventConsumer, maxIngressEventCount_);
            metrics_.drainedEventCount += ingressDrain.drainedEventCount;
            if (ingressDrain.stopReason == WorldIngressDrainStopReason::BudgetExhausted)
            {
                ++metrics_.drainBudgetExhaustionCount;
            }
            if (ingressDrain.stopReason == WorldIngressDrainStopReason::SourceFailure)
            {
                return MakeReport(WorldOwnerIterationStopReason::IngressSourceFailure, ingressDrain,
                                  WorldFixedStepTakeResult::NotDue, WorldTickProcessResult::Processed, 0, false);
            }

            const WorldFixedStepSchedule::Clock::time_point firstDueDeadline = schedule_.NextDeadline();
            WorldFixedStepBatch batch;
            const WorldFixedStepTakeResult scheduleTakeResult = schedule_.TryTakeDueTicks(now, &batch);
            if (scheduleTakeResult == WorldFixedStepTakeResult::NotDue)
            {
                return MakeReport(WorldOwnerIterationStopReason::TickNotDue, ingressDrain, scheduleTakeResult,
                                  WorldTickProcessResult::Processed, 0, false);
            }
            if (scheduleTakeResult != WorldFixedStepTakeResult::Taken)
            {
                return MakeReport(WorldOwnerIterationStopReason::ScheduleFailure, ingressDrain, scheduleTakeResult,
                                  WorldTickProcessResult::Processed, 0, false);
            }
            if (batch.overrun)
            {
                ++metrics_.overrunBatchCount;
            }
            const std::chrono::microseconds tickStartLag =
                std::chrono::duration_cast<std::chrono::microseconds>(now - firstDueDeadline);
            if (tickStartLag.count() > 0 &&
                static_cast<std::uint64_t>(tickStartLag.count()) > metrics_.maximumTickStartLagMicroseconds)
            {
                metrics_.maximumTickStartLagMicroseconds = static_cast<std::uint64_t>(tickStartLag.count());
            }

            const std::span<const WorldSession> joinedSessions = sessionRegistry_.JoinedSessions();
            std::span<const WorldPlayerScore> playerScores;
            if constexpr (requires { eventConsumer.GameplayState().PlayerScores(); })
            {
                playerScores = eventConsumer.GameplayState().PlayerScores();
            }

            const float fixedDeltaSeconds = std::chrono::duration<float>(schedule_.FixedStep()).count();
            std::uint32_t processedTickCount = 0;
            for (std::uint32_t offset = 0; offset < batch.stepCount; ++offset)
            {
                const std::uint32_t serverTick = batch.firstServerTick + offset;
                const std::uint64_t takenCommandCountBeforeTick = movementCommandStore_.Metrics().takenCommandCount;
                const WorldFixedStepSchedule::Clock::time_point tickStartedAt = WorldFixedStepSchedule::Clock::now();
                bool shouldProcessSimulation = true;
                if constexpr (requires { eventConsumer.ShouldProcessSimulation(); })
                {
                    shouldProcessSimulation = eventConsumer.ShouldProcessSimulation();
                }
                WorldTickProcessResult tickProcessResult = WorldTickProcessResult::Processed;
                if (shouldProcessSimulation)
                {
                    tickProcessResult = tickProcessor_.Process(serverTick, fixedDeltaSeconds, joinedSessions,
                                                               movementCommandStore_, entityManager_, playerScores);
                }
                const WorldFixedStepSchedule::Clock::time_point tickCompletedAt = WorldFixedStepSchedule::Clock::now();
                const std::chrono::nanoseconds tickDuration =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(tickCompletedAt - tickStartedAt);
                const std::uint64_t tickDurationNanoseconds =
                    tickDuration.count() > 0 ? static_cast<std::uint64_t>(tickDuration.count()) : 1;
                if (tickDurationNanoseconds > metrics_.maximumTickDurationNanoseconds)
                {
                    metrics_.maximumTickDurationNanoseconds = tickDurationNanoseconds;
                }
                if (tickProcessResult != WorldTickProcessResult::Processed)
                {
                    ++metrics_.tickProcessFailureCount;
                    return MakeReport(WorldOwnerIterationStopReason::TickProcessFailure, ingressDrain,
                                      scheduleTakeResult, tickProcessResult, processedTickCount, batch.overrun);
                }

                const std::uint64_t takenCommandCountAfterTick = movementCommandStore_.Metrics().takenCommandCount;
                const std::uint64_t processedCommandCount = takenCommandCountAfterTick - takenCommandCountBeforeTick;
                metrics_.processedCommandCount += processedCommandCount;
                if (processedCommandCount > metrics_.maximumCommandsPerTick)
                {
                    metrics_.maximumCommandsPerTick = processedCommandCount;
                }
                lastCompletedServerTick_ = serverTick;
                ++processedTickCount;
                ++metrics_.processedTickCount;
            }

            const WorldControlledStatePublishReport controlledStatePublish =
                eventConsumer.PublishControlledEntityStates(batch.firstServerTick, lastCompletedServerTick_,
                                                            joinedSessions);
            const std::uint64_t nextServerTickAfterBatch = schedule_.NextServerTick();
            if (nextServerTickAfterBatch <= MaximumServerTick)
            {
                eventConsumer.UpdateTickContext(static_cast<std::uint32_t>(nextServerTickAfterBatch),
                                                lastCompletedServerTick_);
            }
            WorldOwnerIterationReport report =
                MakeReport(WorldOwnerIterationStopReason::Completed, ingressDrain, scheduleTakeResult,
                           WorldTickProcessResult::Processed, processedTickCount, batch.overrun);
            report.controlledStatePublish = controlledStatePublish;
            return report;
        }

        [[nodiscard]] const WorldFixedStepSchedule& Schedule() const noexcept
        {
            return schedule_;
        }

        [[nodiscard]] std::uint32_t LastCompletedServerTick() const noexcept
        {
            return lastCompletedServerTick_;
        }

        [[nodiscard]] WorldOwnerMetrics Metrics() const noexcept
        {
            return metrics_;
        }

    private:
        [[nodiscard]] WorldOwnerIterationReport MakeReport(const WorldOwnerIterationStopReason stopReason,
                                                           const WorldIngressDrainReport& ingressDrain,
                                                           const WorldFixedStepTakeResult scheduleTakeResult,
                                                           const WorldTickProcessResult tickProcessResult,
                                                           const std::uint32_t processedTickCount,
                                                           const bool overrun) const noexcept
        {
            return WorldOwnerIterationReport{
                stopReason,
                ingressDrain,
                WorldControlledStatePublishReport{},
                scheduleTakeResult,
                tickProcessResult,
                processedTickCount,
                lastCompletedServerTick_,
                overrun,
            };
        }

        WorldFixedStepSchedule schedule_;
        WorldSessionRegistry& sessionRegistry_;
        WorldMovementCommandStore& movementCommandStore_;
        WorldEntityManager& entityManager_;
        WorldTickProcessor tickProcessor_;
        std::size_t maxIngressEventCount_ = 0;
        std::uint32_t lastCompletedServerTick_ = 0;
        WorldOwnerMetrics metrics_;
    };
} // namespace psnr::world
