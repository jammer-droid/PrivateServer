#pragma once

#include "WorldClock.h"
#include "WorldIngressEventConsumer.h"
#include "WorldIngressDoubleBuffer.h"
#include "WorldMovementCommandStore.h"
#include "WorldOutboundDoubleBuffer.h"
#include "WorldSessionRegistry.h"
#include "WorldTickProcessor.h"
#include "WorldTickSampleBuffer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace psnr::world
{
    struct WorldDoubleBufferedTickConfig final
    {
        std::chrono::nanoseconds fixedStep{};
        std::uint32_t maxCatchUpSteps = 0;
        std::uint64_t initialEpoch = 0;
        std::uint32_t initialServerTick = 0;
        std::uint32_t initialLastCompletedServerTick = 0;
        WorldClock::time_point firstSealDeadline{};
        WorldOutboundMode outboundMode = WorldOutboundMode::Direct;
        WorldTickProcessorConfig tickProcessor{};
    };

    struct WorldDoubleBufferedTickPlan final
    {
        std::uint64_t epoch = 0;
        std::uint32_t serverTick = 0;
        WorldClock::time_point sealDeadline{};
    };

    enum class WorldDoubleBufferedTickStopReason : std::uint8_t
    {
        Completed = 0,
        InvalidConfig,
        IngressAcquireFailed,
        TickProcessFailed,
        OutboundWriteFailed,
        OutboundPrepareFailed,
        OutboundSealFailed,
        IngressReleaseFailed,
        SequenceExhausted,
    };

    struct WorldDoubleBufferedTickReport final
    {
        WorldDoubleBufferedTickStopReason stopReason = WorldDoubleBufferedTickStopReason::Completed;
        WorldDoubleBufferedTickPlan plan{};
        std::size_t consumedEventCount = 0;
        WorldIngressDoubleBufferExchangeResult exchangeResult = WorldIngressDoubleBufferExchangeResult::Exchanged;
        WorldOutboundDoubleBufferExchangeResult outboundExchangeResult = WorldOutboundDoubleBufferExchangeResult::Empty;
        WorldTickProcessResult tickProcessResult = WorldTickProcessResult::Processed;
        std::uint32_t processedTickCount = 0;
        bool overrun = false;
    };

    struct WorldDoubleBufferedTickMetrics final
    {
        std::uint64_t processedTickCount = 0;
        std::uint64_t consumedEventCount = 0;
        std::uint64_t tickOverrunCount = 0;
        std::uint64_t catchUpBatchCount = 0;
        std::uint64_t catchUpTickCount = 0;
        std::uint64_t overrunBatchCount = 0;
        std::uint64_t currentBacklogTickCount = 0;
        std::uint64_t maximumBacklogTickCount = 0;
        std::uint64_t consecutiveOverrunBatchCount = 0;
        std::uint64_t maximumTickStartLagNanoseconds = 0;
        std::uint64_t maximumTickDurationNanoseconds = 0;
        std::uint64_t publishedSnapshotCount = 0;
        std::uint64_t suppressedCatchUpSnapshotCount = 0;
        std::uint64_t publishedOverviewCount = 0;
        std::uint64_t suppressedCatchUpOverviewCount = 0;
    };

    // pump가 현재 plan의 deadline/epoch로 write slot을 seal한 뒤 이 coordinator가 같은 epoch를 소비한다.
    // event consume부터 canonical commit까지 read slot ownership을 유지하고, 기존 예정 시각 기준으로 다음 deadline을 잡는다.
    class WorldDoubleBufferedTickCoordinator final
    {
    public:
        WorldDoubleBufferedTickCoordinator(const WorldDoubleBufferedTickConfig& config,
                                           WorldIngressDoubleBuffer& ingressBuffer,
                                           WorldSessionRegistry& sessionRegistry,
                                           WorldMovementCommandStore& movementCommandStore,
                                           WorldEntityManager& entityManager,
                                           WorldOutboundDoubleBuffer* const outboundBuffer = nullptr,
                                           WorldTickSampleBuffer* const tickSampleBuffer = nullptr) noexcept
            : config_(config)
            , ingressBuffer_(ingressBuffer)
            , sessionRegistry_(sessionRegistry)
            , movementCommandStore_(movementCommandStore)
            , entityManager_(entityManager)
            , outboundBuffer_(outboundBuffer)
            , tickSampleBuffer_(tickSampleBuffer)
            , tickProcessor_(config.tickProcessor)
            , nextPlan_{config.initialEpoch, config.initialServerTick, config.firstSealDeadline}
            , lastCompletedServerTick_(config.initialLastCompletedServerTick)
        {
        }

        template <typename TEventConsumer, typename TClockSource>
        [[nodiscard]] WorldDoubleBufferedTickReport RunNext(
            TEventConsumer& eventConsumer, TClockSource& clockSource,
            const std::chrono::milliseconds outboundPrepareTimeout = std::chrono::milliseconds::zero())
        {
            if (!IsConfigValid())
            {
                return MakeReport(WorldDoubleBufferedTickStopReason::InvalidConfig, 0,
                                  WorldIngressDoubleBufferExchangeResult::InvalidArgument,
                                  WorldOutboundDoubleBufferExchangeResult::InvalidArgument,
                                  WorldTickProcessResult::Processed, 0, false);
            }

            if (config_.outboundMode == WorldOutboundMode::DoubleBuffered)
            {
                const WorldOutboundDoubleBufferExchangeResult prepareResult =
                    outboundBuffer_->WaitPrepareWrite(outboundPrepareTimeout);
                if (prepareResult != WorldOutboundDoubleBufferExchangeResult::Exchanged)
                {
                    return MakeReport(WorldDoubleBufferedTickStopReason::OutboundPrepareFailed, 0,
                                      WorldIngressDoubleBufferExchangeResult::Exchanged, prepareResult,
                                      WorldTickProcessResult::Processed, 0, false);
                }
            }

            WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError> acquireResult =
                ingressBuffer_.WaitAcquireRead(nextPlan_.epoch, std::chrono::milliseconds::zero());
            if (acquireResult.Failed())
            {
                return MakeReport(WorldDoubleBufferedTickStopReason::IngressAcquireFailed, 0,
                                  MapIngressExchangeError(acquireResult.Error()),
                                  WorldOutboundDoubleBufferExchangeResult::Empty, WorldTickProcessResult::Processed, 0,
                                  false);
            }
            WorldIngressReadBatch readBatch = acquireResult.TakeValue();

            const WorldClock::time_point tickStartedAt = clockSource.Now();
            const CatchUpPlan catchUpPlan = CalculateCatchUpPlan(tickStartedAt);
            const std::uint32_t stepCount = catchUpPlan.stepCount;
            const std::uint32_t batchLastServerTick = nextPlan_.serverTick + stepCount - 1;
            if (config_.outboundMode == WorldOutboundMode::DoubleBuffered &&
                outboundBuffer_->BeginWriteBatch(nextPlan_.epoch, nextPlan_.serverTick, batchLastServerTick) !=
                    WorldOutboundDoubleBufferExchangeResult::Exchanged)
            {
                const WorldResult<void, WorldDoubleBufferRoleExchangeError> releaseResult =
                    ingressBuffer_.ReleaseRead(readBatch);
                return MakeReport(WorldDoubleBufferedTickStopReason::OutboundSealFailed, readBatch.events.size(),
                                  releaseResult.Succeeded() ? WorldIngressDoubleBufferExchangeResult::Exchanged
                                                            : MapIngressExchangeError(releaseResult.Error()),
                                  WorldOutboundDoubleBufferExchangeResult::InvalidState,
                                  WorldTickProcessResult::Processed, 0, false);
            }
            std::uint32_t roundIdAtTickStart = 0;
            WorldRoundPhase roundPhaseAtTickStart = WorldRoundPhase::Invalid;
            if constexpr (requires { eventConsumer.GameplayState().RoundState(); })
            {
                const WorldRoundRuntimeState& roundState = eventConsumer.GameplayState().RoundState();
                roundIdAtTickStart = roundState.roundId;
                roundPhaseAtTickStart = roundState.phase;
            }
            eventConsumer.UpdateTickContext(nextPlan_.serverTick, lastCompletedServerTick_);
            eventConsumer.BeginOutboundTick(batchLastServerTick);
            for (const psnr::runtime::NrToWorldEvent& event : readBatch.events)
            {
                static_cast<void>(eventConsumer.Handle(event, WorldInboundMode::DoubleBuffered));
            }

            const float fixedDeltaSeconds = std::chrono::duration<float>(config_.fixedStep).count();

            WorldTickProcessResult tickProcessResult = WorldTickProcessResult::Processed;
            std::uint32_t processedTickCount = 0;
            for (std::uint32_t offset = 0; offset < stepCount; ++offset)
            {
                const std::uint32_t serverTick = nextPlan_.serverTick + offset;
                eventConsumer.UpdateTickContext(serverTick, lastCompletedServerTick_);
                const std::span<const WorldSession> joinedSessions = sessionRegistry_.JoinedSessions();
                std::span<const WorldPlayerScore> playerScores;
                if constexpr (requires { eventConsumer.GameplayState().PlayerScores(); })
                {
                    playerScores = eventConsumer.GameplayState().PlayerScores();
                }
                bool shouldProcessSimulation = true;
                if constexpr (requires { eventConsumer.ShouldProcessSimulation(); })
                {
                    shouldProcessSimulation = eventConsumer.ShouldProcessSimulation();
                }
                if (shouldProcessSimulation)
                {
                    WorldActiveArea resolvedActiveArea;
                    const WorldActiveArea* activeArea = nullptr;
                    if constexpr (requires(WorldActiveArea* outActiveArea) {
                                      eventConsumer.ResolveActiveArea(serverTick, outActiveArea);
                                  })
                    {
                        const WorldActiveAreaResolveResult activeAreaResult =
                            eventConsumer.ResolveActiveArea(serverTick, &resolvedActiveArea);
                        if (activeAreaResult == WorldActiveAreaResolveResult::Resolved)
                        {
                            activeArea = &resolvedActiveArea;
                        }
                        else if (activeAreaResult != WorldActiveAreaResolveResult::Inactive)
                        {
                            tickProcessResult = WorldTickProcessResult::InvalidActiveArea;
                            break;
                        }
                    }
                    tickProcessResult = tickProcessor_.Process(WorldInboundMode::DoubleBuffered, serverTick,
                                                               fixedDeltaSeconds, joinedSessions, movementCommandStore_,
                                                               entityManager_, playerScores, activeArea);

                    if (tickProcessResult == WorldTickProcessResult::Processed)
                    {
                        if constexpr (requires {
                                          eventConsumer.ProcessGameplayTick(
                                              serverTick, tickProcessor_.LastPhysicsResult(),
                                              tickProcessor_.LastCollisionDeathSet(),
                                              tickProcessor_.LastActiveAreaBoundaryDeathSet(), activeArea,
                                              tickProcessor_.LastPlayerSpawnCandidates(), joinedSessions);
                                      })
                        {
                            const WorldGameplayTickRecordResult gameplayResult = eventConsumer.ProcessGameplayTick(
                                serverTick, tickProcessor_.LastPhysicsResult(), tickProcessor_.LastCollisionDeathSet(),
                                tickProcessor_.LastActiveAreaBoundaryDeathSet(), activeArea,
                                tickProcessor_.LastPlayerSpawnCandidates(), joinedSessions);
                            if (gameplayResult != WorldGameplayTickRecordResult::Skipped &&
                                gameplayResult != WorldGameplayTickRecordResult::Recorded)
                            {
                                tickProcessResult = WorldTickProcessResult::GameplayProcessFailed;
                            }
                        }
                        else if constexpr (requires {
                                               eventConsumer.ProcessGameplayTick(
                                                   serverTick, tickProcessor_.LastPhysicsResult(),
                                                   tickProcessor_.LastCollisionDeathSet(),
                                                   tickProcessor_.LastPlayerSpawnCandidates(), joinedSessions);
                                           })
                        {
                            const WorldGameplayTickRecordResult gameplayResult = eventConsumer.ProcessGameplayTick(
                                serverTick, tickProcessor_.LastPhysicsResult(), tickProcessor_.LastCollisionDeathSet(),
                                tickProcessor_.LastPlayerSpawnCandidates(), joinedSessions);
                            if (gameplayResult != WorldGameplayTickRecordResult::Skipped &&
                                gameplayResult != WorldGameplayTickRecordResult::Recorded)
                            {
                                tickProcessResult = WorldTickProcessResult::GameplayProcessFailed;
                            }
                        }
                        else if constexpr (requires {
                                               eventConsumer.ProcessGameplayTick(
                                                   serverTick, tickProcessor_.LastPhysicsResult(),
                                                   tickProcessor_.LastCollisionDeathSet(), joinedSessions);
                                           })
                        {
                            const WorldGameplayTickRecordResult gameplayResult = eventConsumer.ProcessGameplayTick(
                                serverTick, tickProcessor_.LastPhysicsResult(), tickProcessor_.LastCollisionDeathSet(),
                                joinedSessions);
                            if (gameplayResult != WorldGameplayTickRecordResult::Skipped &&
                                gameplayResult != WorldGameplayTickRecordResult::Recorded)
                            {
                                tickProcessResult = WorldTickProcessResult::GameplayProcessFailed;
                            }
                        }
                        else if constexpr (requires {
                                               eventConsumer.ProcessGameplayTick(
                                                   serverTick, tickProcessor_.LastPhysicsResult(), joinedSessions);
                                           })
                        {
                            const WorldGameplayTickRecordResult gameplayResult = eventConsumer.ProcessGameplayTick(
                                serverTick, tickProcessor_.LastPhysicsResult(), joinedSessions);
                            if (gameplayResult != WorldGameplayTickRecordResult::Skipped &&
                                gameplayResult != WorldGameplayTickRecordResult::Recorded)
                            {
                                tickProcessResult = WorldTickProcessResult::GameplayProcessFailed;
                            }
                        }
                    }
                }

                if (tickProcessResult != WorldTickProcessResult::Processed)
                {
                    break;
                }

                const std::span<const WorldSession> committedJoinedSessions = sessionRegistry_.JoinedSessions();
                if (!eventConsumer.RecordDurableTickOutbound(serverTick, batchLastServerTick, committedJoinedSessions))
                {
                    break;
                }

                lastCompletedServerTick_ = serverTick;
                ++processedTickCount;
                ++metrics_.processedTickCount;
            }

            WorldControlledStatePublishReport snapshotReport;
            WorldOverviewPublishReport overviewReport;
            if (processedTickCount == stepCount) // 모든 batch 성공시 최종 snapshot 발행
            {
                const std::span<const WorldSession> committedJoinedSessions = sessionRegistry_.JoinedSessions();
                snapshotReport = eventConsumer.PublishControlledEntityStates(nextPlan_.serverTick, batchLastServerTick,
                                                                             committedJoinedSessions);
                overviewReport = eventConsumer.PublishWorldOverview(nextPlan_.serverTick, batchLastServerTick,
                                                                    committedJoinedSessions);
            }

            WorldOutboundDoubleBufferExchangeResult outboundExchangeResult =
                WorldOutboundDoubleBufferExchangeResult::Empty;
            WorldDoubleBufferedTickStopReason preReleaseStopReason = WorldDoubleBufferedTickStopReason::Completed;
            if (eventConsumer.OutboundBatchFailed())
            {
                preReleaseStopReason = WorldDoubleBufferedTickStopReason::OutboundWriteFailed;
            }
            else if (tickProcessResult == WorldTickProcessResult::Processed &&
                     config_.outboundMode == WorldOutboundMode::DoubleBuffered)
            {
                outboundExchangeResult = outboundBuffer_->SealWrite(nextPlan_.epoch);
                if (outboundExchangeResult != WorldOutboundDoubleBufferExchangeResult::Exchanged &&
                    outboundExchangeResult != WorldOutboundDoubleBufferExchangeResult::Empty)
                {
                    preReleaseStopReason = WorldDoubleBufferedTickStopReason::OutboundSealFailed;
                }
            }

            const std::size_t consumedEventCount = readBatch.events.size();
            const WorldResult<void, WorldDoubleBufferRoleExchangeError> releaseResult =
                ingressBuffer_.ReleaseRead(readBatch);
            const WorldIngressDoubleBufferExchangeResult releaseExchangeResult =
                releaseResult.Succeeded() ? WorldIngressDoubleBufferExchangeResult::Exchanged
                                          : MapIngressExchangeError(releaseResult.Error());
            if (releaseResult.Failed())
            {
                return MakeReport(WorldDoubleBufferedTickStopReason::IngressReleaseFailed, consumedEventCount,
                                  releaseExchangeResult, outboundExchangeResult, tickProcessResult, processedTickCount,
                                  false);
            }
            if (preReleaseStopReason != WorldDoubleBufferedTickStopReason::Completed)
            {
                return MakeReport(preReleaseStopReason, consumedEventCount, releaseExchangeResult,
                                  outboundExchangeResult, tickProcessResult, processedTickCount, false);
            }
            if (tickProcessResult != WorldTickProcessResult::Processed)
            {
                return MakeReport(WorldDoubleBufferedTickStopReason::TickProcessFailed, consumedEventCount,
                                  releaseExchangeResult, outboundExchangeResult, tickProcessResult, processedTickCount,
                                  false);
            }

            const WorldClock::time_point tickCompletedAt = clockSource.Now();
            const std::chrono::nanoseconds tickDuration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(tickCompletedAt - tickStartedAt);
            const std::uint64_t tickDurationNanoseconds =
                tickDuration.count() > 0 ? static_cast<std::uint64_t>(tickDuration.count()) : 0;
            const bool overrun = tickDuration > config_.fixedStep;

            if (tickSampleBuffer_ != nullptr && roundPhaseAtTickStart == WorldRoundPhase::Running)
            {
                const WorldTickSample sample{
                    nextPlan_.epoch,          nextPlan_.serverTick,
                    lastCompletedServerTick_, roundIdAtTickStart,
                    roundPhaseAtTickStart,    processedTickCount,
                    catchUpPlan.dueTickCount, catchUpPlan.tickStartLagNanoseconds,
                    tickDurationNanoseconds,
                };
                static_cast<void>(tickSampleBuffer_->TryRecord(sample));
            }

            metrics_.consumedEventCount += consumedEventCount;
            if (overrun)
            {
                ++metrics_.tickOverrunCount;
            }
            if (tickDurationNanoseconds > metrics_.maximumTickDurationNanoseconds)
            {
                metrics_.maximumTickDurationNanoseconds = tickDurationNanoseconds;
            }
            if (catchUpPlan.tickStartLagNanoseconds > metrics_.maximumTickStartLagNanoseconds)
            {
                metrics_.maximumTickStartLagNanoseconds = catchUpPlan.tickStartLagNanoseconds;
            }
            if (catchUpPlan.dueTickCount > 1)
            {
                ++metrics_.catchUpBatchCount;
            }
            if (processedTickCount > 1)
            {
                metrics_.catchUpTickCount += processedTickCount - 1;
            }
            if (snapshotReport.snapshotPublished)
            {
                ++metrics_.publishedSnapshotCount;
            }
            metrics_.suppressedCatchUpSnapshotCount += snapshotReport.suppressedSnapshotCount;
            if (overviewReport.overviewPublished)
            {
                ++metrics_.publishedOverviewCount;
            }
            metrics_.suppressedCatchUpOverviewCount += overviewReport.suppressedOverviewCount;

            const WorldClock::time_point nextDeadline = nextPlan_.sealDeadline + config_.fixedStep * processedTickCount;
            const std::uint64_t backlogTickCount = CalculateDueTickCount(tickCompletedAt, nextDeadline);
            metrics_.currentBacklogTickCount = backlogTickCount;
            if (backlogTickCount > metrics_.maximumBacklogTickCount)
            {
                metrics_.maximumBacklogTickCount = backlogTickCount;
            }
            if (backlogTickCount != 0)
            {
                ++metrics_.overrunBatchCount;
                ++metrics_.consecutiveOverrunBatchCount;
            }
            else
            {
                metrics_.consecutiveOverrunBatchCount = 0;
            }
            const std::uint64_t lastProcessedServerTick =
                static_cast<std::uint64_t>(nextPlan_.serverTick) + processedTickCount - 1;
            if (nextPlan_.epoch == std::numeric_limits<std::uint64_t>::max() ||
                lastProcessedServerTick == std::numeric_limits<std::uint32_t>::max())
            {
                return MakeReport(WorldDoubleBufferedTickStopReason::SequenceExhausted, consumedEventCount,
                                  releaseExchangeResult, outboundExchangeResult, tickProcessResult, processedTickCount,
                                  overrun);
            }

            const WorldDoubleBufferedTickReport report =
                MakeReport(WorldDoubleBufferedTickStopReason::Completed, consumedEventCount, releaseExchangeResult,
                           outboundExchangeResult, tickProcessResult, processedTickCount, overrun);
            ++nextPlan_.epoch;
            nextPlan_.serverTick += processedTickCount;
            nextPlan_.sealDeadline += config_.fixedStep * processedTickCount;
            return report;
        }

        [[nodiscard]] WorldDoubleBufferedTickPlan NextPlan() const noexcept
        {
            return nextPlan_;
        }

        [[nodiscard]] std::uint32_t LastCompletedServerTick() const noexcept
        {
            return lastCompletedServerTick_;
        }

        [[nodiscard]] WorldDoubleBufferedTickMetrics Metrics() const noexcept
        {
            return metrics_;
        }

        void BindTickSampleBuffer(WorldTickSampleBuffer* const tickSampleBuffer) noexcept
        {
            tickSampleBuffer_ = tickSampleBuffer;
        }

        void UnbindTickSampleBuffer() noexcept
        {
            tickSampleBuffer_ = nullptr;
        }

        void CloseOutbound() noexcept
        {
            if (outboundBuffer_ != nullptr)
            {
                outboundBuffer_->Close();
            }
        }

    private:
        [[nodiscard]] static WorldIngressDoubleBufferExchangeResult MapIngressExchangeError(
            const WorldDoubleBufferRoleExchangeError error) noexcept
        {
            switch (error)
            {
            case WorldDoubleBufferRoleExchangeError::InvalidArgument:
                return WorldIngressDoubleBufferExchangeResult::InvalidArgument;
            case WorldDoubleBufferRoleExchangeError::InvalidState:
                return WorldIngressDoubleBufferExchangeResult::InvalidState;
            case WorldDoubleBufferRoleExchangeError::TimedOut:
                return WorldIngressDoubleBufferExchangeResult::TimedOut;
            case WorldDoubleBufferRoleExchangeError::Closed:
                return WorldIngressDoubleBufferExchangeResult::Closed;
            }
            return WorldIngressDoubleBufferExchangeResult::InvalidState;
        }

        struct CatchUpPlan final
        {
            std::uint64_t dueTickCount = 0;
            std::uint64_t tickStartLagNanoseconds = 0;
            std::uint32_t stepCount = 0;
        };

        [[nodiscard]] bool IsConfigValid() const noexcept
        {
            const bool validInitialTick = config_.initialServerTick == 0
                                              ? config_.initialLastCompletedServerTick == 0
                                              : config_.initialLastCompletedServerTick == config_.initialServerTick - 1;
            const bool validOutbound =
                config_.outboundMode == WorldOutboundMode::Direct
                    ? outboundBuffer_ == nullptr
                    : config_.outboundMode == WorldOutboundMode::DoubleBuffered && outboundBuffer_ != nullptr;
            return config_.fixedStep.count() > 0 && config_.maxCatchUpSteps != 0 && nextPlan_.epoch != 0 &&
                   validInitialTick && validOutbound;
        }

        [[nodiscard]] std::uint64_t CalculateDueTickCount(const WorldClock::time_point now,
                                                          const WorldClock::time_point deadline) const noexcept
        {
            if (now < deadline)
            {
                return 0;
            }

            const WorldClock::duration overdue = now - deadline;
            return static_cast<std::uint64_t>(overdue / config_.fixedStep) + 1;
        }

        // Catch-Up Tick 계산
        [[nodiscard]] CatchUpPlan CalculateCatchUpPlan(const WorldClock::time_point now) const noexcept
        {
            const std::uint64_t dueTickCount = CalculateDueTickCount(now, nextPlan_.sealDeadline);
            const std::uint64_t requiredStepCount = dueTickCount == 0 ? 1 : dueTickCount;
            std::uint64_t tickStartLagNanoseconds = 0;
            if (now > nextPlan_.sealDeadline)
            {
                const std::chrono::nanoseconds tickStartLag =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - nextPlan_.sealDeadline);
                tickStartLagNanoseconds = static_cast<std::uint64_t>(tickStartLag.count());
            }

            // server tick overflow 방지
            const std::uint64_t availableServerTicks =
                static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - nextPlan_.serverTick + 1;
            const std::uint64_t boundedStepCount =
                requiredStepCount < config_.maxCatchUpSteps ? requiredStepCount : config_.maxCatchUpSteps;
            const std::uint32_t stepCount = static_cast<std::uint32_t>(
                boundedStepCount < availableServerTicks ? boundedStepCount : availableServerTicks);

            return CatchUpPlan{dueTickCount, tickStartLagNanoseconds, stepCount};
        }

        [[nodiscard]] WorldDoubleBufferedTickReport MakeReport(
            const WorldDoubleBufferedTickStopReason stopReason, const std::size_t consumedEventCount,
            const WorldIngressDoubleBufferExchangeResult exchangeResult,
            const WorldOutboundDoubleBufferExchangeResult outboundExchangeResult,
            const WorldTickProcessResult tickProcessResult, const std::uint32_t processedTickCount,
            const bool overrun) const noexcept
        {
            return WorldDoubleBufferedTickReport{
                stopReason,
                nextPlan_,
                consumedEventCount,
                exchangeResult,
                outboundExchangeResult,
                tickProcessResult,
                processedTickCount,
                overrun,
            };
        }

        WorldDoubleBufferedTickConfig config_;
        WorldIngressDoubleBuffer& ingressBuffer_;
        WorldSessionRegistry& sessionRegistry_;
        WorldMovementCommandStore& movementCommandStore_;
        WorldEntityManager& entityManager_;
        WorldOutboundDoubleBuffer* outboundBuffer_ = nullptr;
        WorldTickSampleBuffer* tickSampleBuffer_ = nullptr;
        WorldTickProcessor tickProcessor_;
        WorldDoubleBufferedTickPlan nextPlan_;
        std::uint32_t lastCompletedServerTick_ = 0;
        WorldDoubleBufferedTickMetrics metrics_;
    };
} // namespace psnr::world
