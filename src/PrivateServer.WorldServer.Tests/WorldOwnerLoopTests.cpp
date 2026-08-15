#include "pch.h"

#include "WorldOwnerLoop.h"

#include <chrono>

namespace psnr::world::tests
{
    namespace
    {
        class ScriptedEventSource final
        {
        public:
            ScriptedEventSource(const std::size_t eventCount, const psnr::core::NrStatus terminalStatus) noexcept
                : remainingEventCount_(eventCount)
                , terminalStatus_(terminalStatus)
            {
            }

            [[nodiscard]] psnr::core::NrStatus TryPopBatch(psnr::runtime::NrToWorldEvent* const eventBuffer,
                                                           const std::size_t eventBufferCount,
                                                           std::size_t* const outEventCount) noexcept
            {
                if (remainingEventCount_ == 0)
                {
                    return terminalStatus_;
                }

                const std::size_t eventCount =
                    remainingEventCount_ < eventBufferCount ? remainingEventCount_ : eventBufferCount;
                for (std::size_t index = 0; index < eventCount; ++index)
                {
                    eventBuffer[index] = psnr::runtime::NrToWorldEvent{};
                }

                remainingEventCount_ -= eventCount;
                *outEventCount = eventCount;
                return psnr::core::NrStatus::Success();
            }

            void ConfigureWait(const psnr::runtime::NrToWorldWaitResult waitResult,
                               const psnr::core::NrStatus waitStatus = psnr::core::NrStatus::Success()) noexcept
            {
                waitResult_ = waitResult;
                waitStatus_ = waitStatus;
            }

            [[nodiscard]] psnr::core::NrStatus WaitForEvents(
                const std::uint32_t timeoutMilliseconds,
                psnr::runtime::NrToWorldWaitResult* const outWaitResult) noexcept
            {
                ++waitCallCount_;
                lastWaitTimeoutMilliseconds_ = timeoutMilliseconds;
                if (waitStatus_.Failed())
                {
                    return waitStatus_;
                }

                *outWaitResult = waitResult_;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] std::size_t WaitCallCount() const noexcept
            {
                return waitCallCount_;
            }

            [[nodiscard]] std::uint32_t LastWaitTimeoutMilliseconds() const noexcept
            {
                return lastWaitTimeoutMilliseconds_;
            }

            [[nodiscard]] std::size_t RemainingEventCount() const noexcept
            {
                return remainingEventCount_;
            }

        private:
            std::size_t remainingEventCount_ = 0;
            psnr::core::NrStatus terminalStatus_{};
            psnr::runtime::NrToWorldWaitResult waitResult_ = psnr::runtime::NrToWorldWaitResult::TimedOut;
            psnr::core::NrStatus waitStatus_{};
            std::size_t waitCallCount_ = 0;
            std::uint32_t lastWaitTimeoutMilliseconds_ = 0;
        };

        class MovementStoringConsumer final
        {
        public:
            MovementStoringConsumer(const WorldSession& session, WorldMovementCommandStore& commandStore,
                                    const bool storeMovementOnHandle) noexcept
                : session_(session)
                , commandStore_(commandStore)
                , storeMovementOnHandle_(storeMovementOnHandle)
            {
            }

            void UpdateTickContext(const std::uint32_t currentServerTick,
                                   const std::uint32_t lastCompletedServerTick) noexcept
            {
                currentServerTick_ = currentServerTick;
                lastCompletedServerTick_ = lastCompletedServerTick;
            }

            void Handle(const psnr::runtime::NrToWorldEvent&)
            {
                ++handledEventCount_;
                if (storeMovementOnHandle_)
                {
                    const WorldMovementCommand command{
                        session_.sessionKey,
                        session_.playerId,
                        session_.entityKey,
                        currentServerTick_,
                        currentServerTick_,
                        1.0f,
                        0.0f,
                    };
                    storeResult_ = commandStore_.TryStore(command);
                    storeMovementOnHandle_ = false;
                }
            }

            [[nodiscard]] WorldControlledStatePublishReport PublishControlledEntityStates(
                const std::uint32_t firstProcessedServerTick, const std::uint32_t lastProcessedServerTick,
                const std::span<const WorldSession> joinedSessions) noexcept
            {
                ++publishCallCount_;
                firstPublishedServerTick_ = firstProcessedServerTick;
                lastPublishedServerTick_ = lastProcessedServerTick;
                const std::uint32_t sessionCount = static_cast<std::uint32_t>(joinedSessions.size());
                return WorldControlledStatePublishReport{sessionCount, sessionCount, 0};
            }

            [[nodiscard]] std::uint32_t CurrentServerTick() const noexcept
            {
                return currentServerTick_;
            }

            [[nodiscard]] std::uint32_t LastCompletedServerTick() const noexcept
            {
                return lastCompletedServerTick_;
            }

            [[nodiscard]] std::size_t HandledEventCount() const noexcept
            {
                return handledEventCount_;
            }

            [[nodiscard]] WorldMovementCommandStoreResult StoreResult() const noexcept
            {
                return storeResult_;
            }

            [[nodiscard]] std::size_t PublishCallCount() const noexcept
            {
                return publishCallCount_;
            }

            [[nodiscard]] std::uint32_t LastPublishedServerTick() const noexcept
            {
                return lastPublishedServerTick_;
            }

            [[nodiscard]] std::uint32_t FirstPublishedServerTick() const noexcept
            {
                return firstPublishedServerTick_;
            }

        private:
            WorldSession session_{};
            WorldMovementCommandStore& commandStore_;
            bool storeMovementOnHandle_ = false;
            std::uint32_t currentServerTick_ = 0;
            std::uint32_t lastCompletedServerTick_ = 0;
            std::size_t handledEventCount_ = 0;
            std::size_t publishCallCount_ = 0;
            std::uint32_t firstPublishedServerTick_ = 0;
            std::uint32_t lastPublishedServerTick_ = 0;
            WorldMovementCommandStoreResult storeResult_ = WorldMovementCommandStoreResult::InvalidCommand;
        };

        class ScriptedClockSource final
        {
        public:
            ScriptedClockSource(const WorldFixedStepSchedule::Clock::time_point first,
                                const WorldFixedStepSchedule::Clock::time_point second) noexcept
                : first_(first)
                , second_(second)
            {
            }

            [[nodiscard]] WorldFixedStepSchedule::Clock::time_point Now() noexcept
            {
                ++callCount_;
                return callCount_ == 1 ? first_ : second_;
            }

            [[nodiscard]] std::size_t CallCount() const noexcept
            {
                return callCount_;
            }

        private:
            WorldFixedStepSchedule::Clock::time_point first_{};
            WorldFixedStepSchedule::Clock::time_point second_{};
            std::size_t callCount_ = 0;
        };

        [[nodiscard]] WorldFixedStepSchedule MakeSchedule(const std::uint32_t maxCatchUpSteps,
                                                          const std::uint32_t tickRateHz = 10)
        {
            const WorldFixedStepScheduleConfig config{tickRateHz, maxCatchUpSteps, 100};
            WorldResult<WorldFixedStepSchedule> result =
                CreateWorldFixedStepSchedule(config, WorldFixedStepSchedule::Clock::time_point{});
            EXPECT_TRUE(result.Succeeded());
            return result.TakeValue();
        }

        [[nodiscard]] WorldEntityComponents MakeComponents()
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{10.0f, 20.0f, 0.0f};
            components.movementCapability = MovementCapabilityComponent{10.0f};
            components.playerControl = PlayerControlComponent{7};
            return components;
        }

        [[nodiscard]] WorldSession CreateJoinedSession(WorldSessionRegistry& sessionRegistry,
                                                       WorldEntityManager& entityManager, EntityHandle* const outHandle)
        {
            WorldEntityKey entityKey;
            EXPECT_TRUE(entityManager.TryCreate(MakeComponents(), &entityKey, outHandle));
            const WorldSessionKey sessionKey{10};
            EXPECT_TRUE(sessionRegistry.TryRegister(sessionKey));
            EXPECT_TRUE(sessionRegistry.TryBindPlayer(sessionKey, 7, entityKey));
            return WorldSession{sessionKey, 7, entityKey};
        }
    } // namespace

    TEST(WorldOwnerLoopTests, DrainsIngressBeforeProcessingDueTick)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        EntityHandle handle;
        const WorldSession session = CreateJoinedSession(sessionRegistry, entityManager, &handle);
        MovementStoringConsumer consumer{session, commandStore, true};
        ScriptedEventSource source{
            1,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        const WorldFixedStepSchedule::Clock::time_point now =
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100};

        const WorldOwnerIterationReport report = ownerLoop.RunIteration(source, consumer, now);

        WorldEntityComponents actual;
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &actual));
        EXPECT_EQ(report.stopReason, WorldOwnerIterationStopReason::Completed);
        EXPECT_EQ(report.ingressDrain.drainedEventCount, 1u);
        EXPECT_EQ(report.processedTickCount, 1u);
        EXPECT_EQ(report.lastCompletedServerTick, 100u);
        EXPECT_EQ(report.controlledStatePublish.attempted, 1u);
        EXPECT_EQ(report.controlledStatePublish.submitted, 1u);
        EXPECT_EQ(consumer.HandledEventCount(), 1u);
        EXPECT_EQ(consumer.PublishCallCount(), 1u);
        EXPECT_EQ(consumer.FirstPublishedServerTick(), 100u);
        EXPECT_EQ(consumer.LastPublishedServerTick(), 100u);
        EXPECT_EQ(consumer.StoreResult(), WorldMovementCommandStoreResult::Stored);
        EXPECT_EQ(consumer.CurrentServerTick(), 101u);
        EXPECT_EQ(consumer.LastCompletedServerTick(), 100u);
        EXPECT_FLOAT_EQ(actual.transform.positionX, 11.0f);
        EXPECT_EQ(ownerLoop.Metrics().drainedEventCount, 1u);
        EXPECT_EQ(ownerLoop.Metrics().processedTickCount, 1u);
    }

    TEST(WorldOwnerLoopTests, BurstIngressRespectsDrainBudgetWithoutStarvingDueTick)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        MovementStoringConsumer consumer{WorldSession{}, commandStore, false};
        ScriptedEventSource source{
            5,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 2, 99};
        const WorldFixedStepSchedule::Clock::time_point now =
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100};

        const WorldOwnerIterationReport firstReport = ownerLoop.RunIteration(source, consumer, now);
        const WorldOwnerIterationReport secondReport = ownerLoop.RunIteration(source, consumer, now);
        const WorldOwnerIterationReport thirdReport = ownerLoop.RunIteration(source, consumer, now);

        EXPECT_EQ(firstReport.stopReason, WorldOwnerIterationStopReason::Completed);
        EXPECT_EQ(firstReport.ingressDrain.stopReason, WorldIngressDrainStopReason::BudgetExhausted);
        EXPECT_EQ(firstReport.ingressDrain.drainedEventCount, 2u);
        EXPECT_EQ(firstReport.processedTickCount, 1u);

        EXPECT_EQ(secondReport.stopReason, WorldOwnerIterationStopReason::TickNotDue);
        EXPECT_EQ(secondReport.ingressDrain.stopReason, WorldIngressDrainStopReason::BudgetExhausted);
        EXPECT_EQ(secondReport.ingressDrain.drainedEventCount, 2u);
        EXPECT_EQ(secondReport.processedTickCount, 0u);

        EXPECT_EQ(thirdReport.stopReason, WorldOwnerIterationStopReason::TickNotDue);
        EXPECT_EQ(thirdReport.ingressDrain.stopReason, WorldIngressDrainStopReason::QueueEmpty);
        EXPECT_EQ(thirdReport.ingressDrain.drainedEventCount, 1u);
        EXPECT_EQ(thirdReport.processedTickCount, 0u);

        EXPECT_EQ(source.RemainingEventCount(), 0u);
        EXPECT_EQ(consumer.HandledEventCount(), 5u);
        EXPECT_EQ(ownerLoop.Metrics().drainedEventCount, 5u);
        EXPECT_EQ(ownerLoop.Metrics().drainBudgetExhaustionCount, 2u);
        EXPECT_EQ(ownerLoop.Metrics().processedTickCount, 1u);
    }

    TEST(WorldOwnerLoopTests, DueTickSkipsIngressWait)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        const WorldFixedStepSchedule::Clock::time_point now =
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100};

        const WorldOwnerWaitReport report = ownerLoop.WaitForNextIteration(source, now);

        EXPECT_EQ(report.wakeReason, WorldOwnerWakeReason::TickDeadline);
        EXPECT_EQ(report.waitTimeoutMilliseconds, 0u);
        EXPECT_EQ(source.WaitCallCount(), 0u);
    }

    TEST(WorldOwnerLoopTests, IngressWaitRoundsUpToNextTickDeadline)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::EventsAvailable);
        WorldOwnerLoop ownerLoop{MakeSchedule(3, 60), sessionRegistry, commandStore, entityManager, 4, 99};

        const WorldOwnerWaitReport report =
            ownerLoop.WaitForNextIteration(source, WorldFixedStepSchedule::Clock::time_point{});

        EXPECT_EQ(report.wakeReason, WorldOwnerWakeReason::EventsAvailable);
        EXPECT_EQ(report.waitTimeoutMilliseconds, 17u);
        EXPECT_EQ(source.WaitCallCount(), 1u);
        EXPECT_EQ(source.LastWaitTimeoutMilliseconds(), 17u);
    }

    TEST(WorldOwnerLoopTests, IngressTimeoutAndCloseMapToDistinctWakeReasons)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};

        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::TimedOut);
        const WorldOwnerWaitReport timedOut =
            ownerLoop.WaitForNextIteration(source, WorldFixedStepSchedule::Clock::time_point{});
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::Closed);
        const WorldOwnerWaitReport closed =
            ownerLoop.WaitForNextIteration(source, WorldFixedStepSchedule::Clock::time_point{});

        EXPECT_EQ(timedOut.wakeReason, WorldOwnerWakeReason::TickDeadline);
        EXPECT_EQ(closed.wakeReason, WorldOwnerWakeReason::IngressClosed);
    }

    TEST(WorldOwnerLoopTests, IngressWaitFailureIsReportedWithoutChangingSchedule)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::TimedOut,
                             psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 123));
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};

        const WorldOwnerWaitReport report =
            ownerLoop.WaitForNextIteration(source, WorldFixedStepSchedule::Clock::time_point{});

        EXPECT_EQ(report.wakeReason, WorldOwnerWakeReason::IngressSourceFailure);
        EXPECT_EQ(report.sourceStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        EXPECT_EQ(report.sourceStatus.NativeErrorCode(), 123u);
        EXPECT_EQ(ownerLoop.Schedule().NextServerTick(), 100u);
    }

    TEST(WorldOwnerLoopTests, RunNextRechecksClockAfterEventWakeBeforeRunningIteration)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        EntityHandle handle;
        const WorldSession session = CreateJoinedSession(sessionRegistry, entityManager, &handle);
        MovementStoringConsumer consumer{session, commandStore, false};
        ScriptedEventSource source{
            1,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::EventsAvailable);
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        ScriptedClockSource clock{
            WorldFixedStepSchedule::Clock::time_point{},
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{50},
        };

        const WorldOwnerStepReport report = ownerLoop.RunNext(source, consumer, clock);

        EXPECT_EQ(report.stopReason, WorldOwnerStepStopReason::IterationCompleted);
        EXPECT_TRUE(report.iterationRan);
        EXPECT_EQ(report.wait.wakeReason, WorldOwnerWakeReason::EventsAvailable);
        EXPECT_EQ(report.iteration.stopReason, WorldOwnerIterationStopReason::TickNotDue);
        EXPECT_EQ(report.iteration.ingressDrain.drainedEventCount, 1u);
        EXPECT_EQ(clock.CallCount(), 2u);
    }

    TEST(WorldOwnerLoopTests, RunNextProcessesTickAfterDeadlineTimeout)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        MovementStoringConsumer consumer{WorldSession{}, commandStore, false};
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::TimedOut);
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        ScriptedClockSource clock{
            WorldFixedStepSchedule::Clock::time_point{},
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100},
        };

        const WorldOwnerStepReport report = ownerLoop.RunNext(source, consumer, clock);

        EXPECT_EQ(report.stopReason, WorldOwnerStepStopReason::IterationCompleted);
        EXPECT_TRUE(report.iterationRan);
        EXPECT_EQ(report.wait.wakeReason, WorldOwnerWakeReason::TickDeadline);
        EXPECT_EQ(report.iteration.processedTickCount, 1u);
        EXPECT_EQ(report.iteration.lastCompletedServerTick, 100u);
        EXPECT_EQ(clock.CallCount(), 2u);
    }

    TEST(WorldOwnerLoopTests, RunNextStopsBeforeIterationWhenIngressIsClosed)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        MovementStoringConsumer consumer{WorldSession{}, commandStore, false};
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::Closed);
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        ScriptedClockSource clock{
            WorldFixedStepSchedule::Clock::time_point{},
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100},
        };

        const WorldOwnerStepReport report = ownerLoop.RunNext(source, consumer, clock);

        EXPECT_EQ(report.stopReason, WorldOwnerStepStopReason::IngressClosed);
        EXPECT_FALSE(report.iterationRan);
        EXPECT_EQ(clock.CallCount(), 1u);
        EXPECT_EQ(source.WaitCallCount(), 1u);
    }

    TEST(WorldOwnerLoopTests, RunNextSeparatesWaitFailureFromIterationFailure)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        MovementStoringConsumer consumer{WorldSession{}, commandStore, false};
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 456),
        };
        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::TimedOut,
                             psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 123));
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        ScriptedClockSource waitFailureClock{
            WorldFixedStepSchedule::Clock::time_point{},
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100},
        };

        const WorldOwnerStepReport waitFailure = ownerLoop.RunNext(source, consumer, waitFailureClock);

        EXPECT_EQ(waitFailure.stopReason, WorldOwnerStepStopReason::WaitFailed);
        EXPECT_FALSE(waitFailure.iterationRan);
        EXPECT_EQ(waitFailureClock.CallCount(), 1u);

        source.ConfigureWait(psnr::runtime::NrToWorldWaitResult::TimedOut);
        ScriptedClockSource iterationFailureClock{
            WorldFixedStepSchedule::Clock::time_point{},
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100},
        };
        const WorldOwnerStepReport iterationFailure = ownerLoop.RunNext(source, consumer, iterationFailureClock);

        EXPECT_EQ(iterationFailure.stopReason, WorldOwnerStepStopReason::IterationFailed);
        EXPECT_TRUE(iterationFailure.iterationRan);
        EXPECT_EQ(iterationFailure.iteration.stopReason, WorldOwnerIterationStopReason::IngressSourceFailure);
        EXPECT_EQ(iterationFailureClock.CallCount(), 2u);
    }

    TEST(WorldOwnerLoopTests, ProcessesEntireBoundedCatchUpBatchInServerTickOrder)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        EntityHandle handle;
        const WorldSession session = CreateJoinedSession(sessionRegistry, entityManager, &handle);
        const WorldMovementCommand command{
            session.sessionKey, session.playerId, session.entityKey, 100, 100, 1.0f, 0.0f,
        };
        ASSERT_EQ(commandStore.TryStore(command), WorldMovementCommandStoreResult::Stored);
        MovementStoringConsumer consumer{session, commandStore, false};
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        const WorldFixedStepSchedule::Clock::time_point now =
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{450};

        const WorldOwnerIterationReport report = ownerLoop.RunIteration(source, consumer, now);

        WorldEntityComponents actual;
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &actual));
        EXPECT_EQ(report.stopReason, WorldOwnerIterationStopReason::Completed);
        EXPECT_EQ(report.processedTickCount, 3u);
        EXPECT_EQ(report.lastCompletedServerTick, 102u);
        EXPECT_TRUE(report.overrun);
        EXPECT_EQ(ownerLoop.Schedule().NextServerTick(), 103u);
        EXPECT_EQ(consumer.CurrentServerTick(), 103u);
        EXPECT_EQ(consumer.PublishCallCount(), 1u);
        EXPECT_EQ(consumer.FirstPublishedServerTick(), 100u);
        EXPECT_EQ(consumer.LastPublishedServerTick(), 102u);
        EXPECT_FLOAT_EQ(actual.transform.positionX, 13.0f);

        const WorldOwnerMetrics metrics = ownerLoop.Metrics();
        EXPECT_EQ(metrics.processedTickCount, 3u);
        EXPECT_EQ(metrics.overrunBatchCount, 1u);
        EXPECT_EQ(metrics.tickProcessFailureCount, 0u);
        EXPECT_EQ(metrics.processedCommandCount, 1u);
        EXPECT_EQ(metrics.maximumCommandsPerTick, 1u);
        EXPECT_EQ(metrics.maximumTickStartLagMicroseconds, 350000u);
        EXPECT_GT(metrics.maximumTickDurationNanoseconds, 0u);
    }

    TEST(WorldOwnerLoopTests, SourceFailureDoesNotConsumeDueTick)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        EntityHandle handle;
        const WorldSession session = CreateJoinedSession(sessionRegistry, entityManager, &handle);
        MovementStoringConsumer consumer{session, commandStore, false};
        ScriptedEventSource source{
            0,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 123),
        };
        WorldOwnerLoop ownerLoop{MakeSchedule(3), sessionRegistry, commandStore, entityManager, 4, 99};
        const WorldFixedStepSchedule::Clock::time_point now =
            WorldFixedStepSchedule::Clock::time_point{} + std::chrono::milliseconds{100};

        const WorldOwnerIterationReport report = ownerLoop.RunIteration(source, consumer, now);

        WorldEntityComponents actual;
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &actual));
        EXPECT_EQ(report.stopReason, WorldOwnerIterationStopReason::IngressSourceFailure);
        EXPECT_EQ(report.processedTickCount, 0u);
        EXPECT_EQ(ownerLoop.Schedule().NextServerTick(), 100u);
        EXPECT_EQ(ownerLoop.LastCompletedServerTick(), 99u);
        EXPECT_EQ(actual, MakeComponents());
    }
} // namespace psnr::world::tests
