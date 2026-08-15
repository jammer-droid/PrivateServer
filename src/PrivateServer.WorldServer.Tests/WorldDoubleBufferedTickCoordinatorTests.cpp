#include "pch.h"

#include "WorldDoubleBufferedTickCoordinator.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace psnr::world::tests
{
    namespace
    {
        class FakeCoordinatorClock final
        {
        public:
            [[nodiscard]] WorldClock::time_point Now() const noexcept
            {
                return now_;
            }

            void Advance(const std::chrono::milliseconds duration) noexcept
            {
                now_ += duration;
            }

        private:
            WorldClock::time_point now_{};
        };

        class FakeCoordinatorGameplayState final
        {
        public:
            void SetRoundState(const WorldRoundRuntimeState& roundState) noexcept
            {
                roundState_ = roundState;
            }

            [[nodiscard]] const WorldRoundRuntimeState& RoundState() const noexcept
            {
                return roundState_;
            }

        private:
            WorldRoundRuntimeState roundState_{};
        };

        class FakeCoordinatorEventConsumer final
        {
        public:
            FakeCoordinatorEventConsumer(FakeCoordinatorClock& clock, const std::chrono::milliseconds handleDuration,
                                         WorldOutboundDoubleBuffer* const outboundBuffer = nullptr) noexcept
                : clock_(clock)
                , handleDuration_(handleDuration)
                , outboundBuffer_(outboundBuffer)
            {
            }

            void UpdateTickContext(const std::uint32_t currentServerTick,
                                   const std::uint32_t lastCompletedServerTick) noexcept
            {
                currentServerTick_ = currentServerTick;
                lastCompletedServerTick_ = lastCompletedServerTick;
            }

            void BeginOutboundTick(const std::uint32_t batchLastServerTick) noexcept
            {
                batchLastServerTick_ = batchLastServerTick;
                outboundBatchFailed_ = false;
            }

            [[nodiscard]] std::uint8_t Handle(const psnr::runtime::NrToWorldEvent&,
                                              const WorldInboundMode inboundMode) noexcept
            {
                ++handledEventCount_;
                inboundMode_ = inboundMode;
                clock_.Advance(handleDuration_);
                return 0;
            }

            [[nodiscard]] bool RecordDurableTickOutbound(const std::uint32_t serverTick,
                                                         const std::uint32_t batchLastServerTick,
                                                         const std::span<const WorldSession>) noexcept
            {
                if (durableTickCount_ < durableServerTicks_.size())
                {
                    durableServerTicks_[durableTickCount_] = serverTick;
                }
                ++durableTickCount_;
                durableBatchLastServerTick_ = batchLastServerTick;
                return true;
            }

            [[nodiscard]] WorldControlledStatePublishReport PublishControlledEntityStates(
                const std::uint32_t firstProcessedServerTick, const std::uint32_t lastProcessedServerTick,
                const std::span<const WorldSession>) noexcept
            {
                ++publishCallCount_;
                publishFirstServerTick_ = firstProcessedServerTick;
                publishLastServerTick_ = lastProcessedServerTick;
                publishObservedGameplay_ = gameplayProcessed_;
                WorldControlledStatePublishReport report;
                report.snapshotPublished = true;
                report.suppressedSnapshotCount = lastProcessedServerTick - firstProcessedServerTick;
                if (outboundBuffer_ == nullptr)
                {
                    return report;
                }

                const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
                const std::array<std::byte, 1> payload{std::byte{1}};
                outboundBatchFailed_ = outboundBuffer_->TryAppend(psnr::core::NrPacketType{1}, recipients, payload) !=
                                       WorldOutboundAppendResult::Appended;
                return report;
            }

            [[nodiscard]] WorldOverviewPublishReport PublishWorldOverview(const std::uint32_t firstProcessedServerTick,
                                                                          const std::uint32_t lastProcessedServerTick,
                                                                          const std::span<const WorldSession>) noexcept
            {
                WorldOverviewPublishReport report;
                report.overviewPublished = true;
                report.suppressedOverviewCount = lastProcessedServerTick - firstProcessedServerTick;
                return report;
            }

            [[nodiscard]] WorldGameplayTickRecordResult ProcessGameplayTick(
                const std::uint32_t serverTick, const WorldPhysicsStepResult&,
                const std::span<const WorldSession>) noexcept
            {
                gameplayProcessed_ = true;
                if (gameplayTickCount_ < gameplayServerTicks_.size())
                {
                    gameplayServerTicks_[gameplayTickCount_] = serverTick;
                }
                ++gameplayTickCount_;
                if (serverTick == stopSimulationAfterGameplayServerTick_)
                {
                    shouldProcessSimulation_ = false;
                }
                if (serverTick == endRoundAfterGameplayServerTick_)
                {
                    const WorldRoundRuntimeState& currentRoundState = gameplayState_.RoundState();
                    gameplayState_.SetRoundState(
                        WorldRoundRuntimeState{currentRoundState.roundId, WorldRoundPhase::Ended, serverTick, 7});
                }
                if (serverTick == gameplayFailureServerTick_)
                {
                    return WorldGameplayTickRecordResult::CommitFailed;
                }
                return WorldGameplayTickRecordResult::Recorded;
            }

            void FailGameplayAt(const std::uint32_t serverTick) noexcept
            {
                gameplayFailureServerTick_ = serverTick;
            }

            void StopSimulationAfterGameplayTick(const std::uint32_t serverTick) noexcept
            {
                stopSimulationAfterGameplayServerTick_ = serverTick;
            }

            void EndRoundAfterGameplayTick(const std::uint32_t serverTick) noexcept
            {
                endRoundAfterGameplayServerTick_ = serverTick;
            }

            [[nodiscard]] bool ShouldProcessSimulation() const noexcept
            {
                return shouldProcessSimulation_;
            }

            void SetRoundState(const WorldRoundRuntimeState& roundState) noexcept
            {
                gameplayState_.SetRoundState(roundState);
            }

            [[nodiscard]] const FakeCoordinatorGameplayState& GameplayState() const noexcept
            {
                return gameplayState_;
            }

            [[nodiscard]] bool OutboundBatchFailed() const noexcept
            {
                return outboundBatchFailed_;
            }

            [[nodiscard]] std::size_t HandledEventCount() const noexcept
            {
                return handledEventCount_;
            }

            [[nodiscard]] WorldInboundMode InboundMode() const noexcept
            {
                return inboundMode_;
            }

            [[nodiscard]] std::uint32_t CurrentServerTick() const noexcept
            {
                return currentServerTick_;
            }

            [[nodiscard]] std::uint32_t LastCompletedServerTick() const noexcept
            {
                return lastCompletedServerTick_;
            }

            [[nodiscard]] bool PublishObservedGameplay() const noexcept
            {
                return publishObservedGameplay_;
            }

            [[nodiscard]] std::size_t GameplayTickCount() const noexcept
            {
                return gameplayTickCount_;
            }

            [[nodiscard]] std::uint32_t GameplayServerTick(const std::size_t index) const noexcept
            {
                return gameplayServerTicks_[index];
            }

            [[nodiscard]] std::size_t DurableTickCount() const noexcept
            {
                return durableTickCount_;
            }

            [[nodiscard]] std::uint32_t DurableServerTick(const std::size_t index) const noexcept
            {
                return durableServerTicks_[index];
            }

            [[nodiscard]] std::uint32_t DurableBatchLastServerTick() const noexcept
            {
                return durableBatchLastServerTick_;
            }

            [[nodiscard]] std::uint32_t OutboundBatchLastServerTick() const noexcept
            {
                return batchLastServerTick_;
            }

            [[nodiscard]] std::size_t PublishCallCount() const noexcept
            {
                return publishCallCount_;
            }

            [[nodiscard]] std::uint32_t PublishFirstServerTick() const noexcept
            {
                return publishFirstServerTick_;
            }

            [[nodiscard]] std::uint32_t PublishLastServerTick() const noexcept
            {
                return publishLastServerTick_;
            }

        private:
            FakeCoordinatorClock& clock_;
            std::chrono::milliseconds handleDuration_{};
            WorldOutboundDoubleBuffer* outboundBuffer_ = nullptr;
            std::size_t handledEventCount_ = 0;
            WorldInboundMode inboundMode_ = WorldInboundMode::TargetServerTick;
            std::uint32_t currentServerTick_ = 0;
            std::uint32_t lastCompletedServerTick_ = 0;
            bool outboundBatchFailed_ = false;
            bool gameplayProcessed_ = false;
            bool publishObservedGameplay_ = false;
            bool shouldProcessSimulation_ = true;
            FakeCoordinatorGameplayState gameplayState_;
            std::array<std::uint32_t, 8> gameplayServerTicks_{};
            std::size_t gameplayTickCount_ = 0;
            std::uint32_t gameplayFailureServerTick_ = std::numeric_limits<std::uint32_t>::max();
            std::uint32_t stopSimulationAfterGameplayServerTick_ = std::numeric_limits<std::uint32_t>::max();
            std::uint32_t endRoundAfterGameplayServerTick_ = std::numeric_limits<std::uint32_t>::max();
            std::array<std::uint32_t, 8> durableServerTicks_{};
            std::uint32_t batchLastServerTick_ = 0;
            std::size_t durableTickCount_ = 0;
            std::uint32_t durableBatchLastServerTick_ = 0;
            std::size_t publishCallCount_ = 0;
            std::uint32_t publishFirstServerTick_ = 0;
            std::uint32_t publishLastServerTick_ = 0;
        };

        [[nodiscard]] std::unique_ptr<WorldIngressDoubleBuffer> CreateCoordinatorBuffer()
        {
            WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> result = WorldIngressDoubleBuffer::Create(2);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] bool PublishIngressEpoch(WorldIngressDoubleBuffer& buffer, const std::uint64_t epoch,
                                               const std::size_t eventCount)
        {
            WorldResult<WorldIngressWriteBatch, WorldDoubleBufferRoleExchangeError> writeResult =
                buffer.WaitAcquireWrite(0, std::chrono::milliseconds{100});
            if (writeResult.Failed())
            {
                return false;
            }

            WorldIngressWriteBatch writeBatch = writeResult.TakeValue();
            if (eventCount > writeBatch.events.size())
            {
                static_cast<void>(buffer.CommitWrite(writeBatch, 0));
                return false;
            }
            for (std::size_t index = 0; index < eventCount; ++index)
            {
                writeBatch.events[index] = psnr::runtime::NrToWorldEvent{};
            }
            return buffer.CommitWrite(writeBatch, eventCount).Succeeded() &&
                   buffer.WaitSwap(epoch, std::chrono::milliseconds{100}).Succeeded();
        }
    } // namespace

    TEST(WorldDoubleBufferedTickCoordinatorTests, ConsumesOneEpochAndAdvancesAbsoluteDeadline)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 1, 1));

        FakeCoordinatorClock clock;
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{25}};
        consumer.SetRoundState(WorldRoundRuntimeState{4, WorldRoundPhase::Running, 200, 0});
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> sampleBufferResult = WorldTickSampleBuffer::Create(3);
        ASSERT_TRUE(sampleBufferResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> sampleBuffer = sampleBufferResult.TakeValue();
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{10}, 1, 1, 100, 99, WorldClock::time_point{} + std::chrono::milliseconds{10},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager, nullptr, sampleBuffer.get(),
        };

        clock.Advance(std::chrono::milliseconds{10});
        const WorldDoubleBufferedTickReport firstReport = coordinator.RunNext(consumer, clock);

        EXPECT_EQ(firstReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        EXPECT_EQ(firstReport.plan.epoch, 1u);
        EXPECT_EQ(firstReport.plan.serverTick, 100u);
        EXPECT_EQ(firstReport.plan.sealDeadline, WorldClock::time_point{} + std::chrono::milliseconds{10});
        EXPECT_EQ(firstReport.consumedEventCount, 1u);
        EXPECT_EQ(firstReport.processedTickCount, 1u);
        EXPECT_TRUE(firstReport.overrun);
        EXPECT_EQ(consumer.HandledEventCount(), 1u);
        EXPECT_EQ(consumer.InboundMode(), WorldInboundMode::DoubleBuffered);
        EXPECT_EQ(consumer.CurrentServerTick(), 100u);
        EXPECT_EQ(consumer.LastCompletedServerTick(), 99u);
        EXPECT_TRUE(consumer.PublishObservedGameplay());
        EXPECT_EQ(coordinator.LastCompletedServerTick(), 100u);

        const WorldDoubleBufferedTickPlan secondPlan = coordinator.NextPlan();
        EXPECT_EQ(secondPlan.epoch, 2u);
        EXPECT_EQ(secondPlan.serverTick, 101u);
        EXPECT_EQ(secondPlan.sealDeadline, WorldClock::time_point{} + std::chrono::milliseconds{20});
        EXPECT_LT(secondPlan.sealDeadline, clock.Now());

        ASSERT_TRUE(PublishIngressEpoch(*buffer, 2, 0));
        const WorldDoubleBufferedTickReport secondReport = coordinator.RunNext(consumer, clock);
        EXPECT_EQ(secondReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        EXPECT_EQ(secondReport.plan.epoch, 2u);
        EXPECT_EQ(secondReport.plan.serverTick, 101u);
        EXPECT_EQ(secondReport.consumedEventCount, 0u);
        EXPECT_EQ(secondReport.processedTickCount, 1u);
        EXPECT_FALSE(secondReport.overrun);
        EXPECT_EQ(coordinator.NextPlan().serverTick, 102u);
        EXPECT_EQ(coordinator.NextPlan().sealDeadline, WorldClock::time_point{} + std::chrono::milliseconds{30});

        ASSERT_TRUE(PublishIngressEpoch(*buffer, 3, 0));
        const WorldDoubleBufferedTickReport thirdReport = coordinator.RunNext(consumer, clock);
        EXPECT_EQ(thirdReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        EXPECT_EQ(thirdReport.processedTickCount, 1u);
        EXPECT_EQ(coordinator.NextPlan().serverTick, 103u);
        EXPECT_EQ(coordinator.NextPlan().sealDeadline, WorldClock::time_point{} + std::chrono::milliseconds{40});

        const WorldDoubleBufferedTickMetrics metrics = coordinator.Metrics();
        EXPECT_EQ(metrics.processedTickCount, 3u);
        EXPECT_EQ(metrics.consumedEventCount, 1u);
        EXPECT_EQ(metrics.tickOverrunCount, 1u);
        EXPECT_EQ(metrics.catchUpBatchCount, 1u);
        EXPECT_EQ(metrics.catchUpTickCount, 0u);
        EXPECT_EQ(metrics.overrunBatchCount, 2u);
        EXPECT_EQ(metrics.currentBacklogTickCount, 0u);
        EXPECT_EQ(metrics.maximumBacklogTickCount, 2u);
        EXPECT_EQ(metrics.consecutiveOverrunBatchCount, 0u);
        EXPECT_EQ(metrics.maximumTickStartLagNanoseconds, 15000000u);
        EXPECT_EQ(metrics.maximumTickDurationNanoseconds, 25000000u);
        EXPECT_EQ(metrics.publishedSnapshotCount, 3u);
        EXPECT_EQ(metrics.suppressedCatchUpSnapshotCount, 0u);

        const std::span<const WorldTickSample> samples = sampleBuffer->Samples();
        ASSERT_EQ(samples.size(), 3);
        EXPECT_EQ(samples[0].epoch, 1u);
        EXPECT_EQ(samples[0].firstServerTick, 100u);
        EXPECT_EQ(samples[0].lastServerTick, 100u);
        EXPECT_EQ(samples[0].processedTickCount, 1u);
        EXPECT_EQ(samples[0].dueTickCount, 1u);
        EXPECT_EQ(samples[0].startLagNanoseconds, 0u);
        EXPECT_EQ(samples[0].executionDurationNanoseconds, 25000000u);
        EXPECT_EQ(samples[1].epoch, 2u);
        EXPECT_EQ(samples[1].firstServerTick, 101u);
        EXPECT_EQ(samples[1].lastServerTick, 101u);
        EXPECT_EQ(samples[1].dueTickCount, 2u);
        EXPECT_EQ(samples[1].startLagNanoseconds, 15000000u);
        EXPECT_EQ(samples[1].executionDurationNanoseconds, 0u);
        EXPECT_EQ(samples[2].epoch, 3u);
        EXPECT_EQ(samples[2].firstServerTick, 102u);
        EXPECT_EQ(samples[2].lastServerTick, 102u);
        EXPECT_EQ(samples[2].dueTickCount, 1u);
        EXPECT_EQ(samples[2].startLagNanoseconds, 5000000u);
        EXPECT_EQ(samples[2].executionDurationNanoseconds, 0u);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, ProcessesAtMostConfiguredCatchUpSteps)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 1, 1));

        FakeCoordinatorClock clock;
        clock.Advance(std::chrono::milliseconds{50});
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}};
        consumer.SetRoundState(WorldRoundRuntimeState{4, WorldRoundPhase::Running, 200, 0});
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> sampleBufferResult = WorldTickSampleBuffer::Create(1);
        ASSERT_TRUE(sampleBufferResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> sampleBuffer = sampleBufferResult.TakeValue();
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{10}, 4, 1, 100, 99, WorldClock::time_point{} + std::chrono::milliseconds{10},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager, nullptr, sampleBuffer.get(),
        };

        const WorldDoubleBufferedTickReport report = coordinator.RunNext(consumer, clock);

        EXPECT_EQ(report.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        EXPECT_EQ(report.plan.serverTick, 100u);
        EXPECT_EQ(report.processedTickCount, 4u);
        EXPECT_EQ(report.consumedEventCount, 1u);
        EXPECT_EQ(consumer.HandledEventCount(), 1u);
        ASSERT_EQ(consumer.GameplayTickCount(), 4u);
        EXPECT_EQ(consumer.GameplayServerTick(0), 100u);
        EXPECT_EQ(consumer.GameplayServerTick(1), 101u);
        EXPECT_EQ(consumer.GameplayServerTick(2), 102u);
        EXPECT_EQ(consumer.GameplayServerTick(3), 103u);
        ASSERT_EQ(consumer.DurableTickCount(), 4u);
        EXPECT_EQ(consumer.DurableServerTick(0), 100u);
        EXPECT_EQ(consumer.DurableServerTick(1), 101u);
        EXPECT_EQ(consumer.DurableServerTick(2), 102u);
        EXPECT_EQ(consumer.DurableServerTick(3), 103u);
        EXPECT_EQ(consumer.DurableBatchLastServerTick(), 103u);
        EXPECT_EQ(consumer.OutboundBatchLastServerTick(), 103u);
        EXPECT_EQ(consumer.PublishCallCount(), 1u);
        EXPECT_EQ(consumer.PublishFirstServerTick(), 100u);
        EXPECT_EQ(consumer.PublishLastServerTick(), 103u);
        EXPECT_EQ(coordinator.LastCompletedServerTick(), 103u);
        EXPECT_EQ(coordinator.NextPlan().epoch, 2u);
        EXPECT_EQ(coordinator.NextPlan().serverTick, 104u);
        EXPECT_EQ(coordinator.NextPlan().sealDeadline, WorldClock::time_point{} + std::chrono::milliseconds{50});
        const WorldDoubleBufferedTickMetrics metrics = coordinator.Metrics();
        EXPECT_EQ(metrics.processedTickCount, 4u);
        EXPECT_EQ(metrics.catchUpBatchCount, 1u);
        EXPECT_EQ(metrics.catchUpTickCount, 3u);
        EXPECT_EQ(metrics.overrunBatchCount, 1u);
        EXPECT_EQ(metrics.currentBacklogTickCount, 1u);
        EXPECT_EQ(metrics.maximumBacklogTickCount, 1u);
        EXPECT_EQ(metrics.consecutiveOverrunBatchCount, 1u);
        EXPECT_EQ(metrics.maximumTickStartLagNanoseconds, 40000000u);
        EXPECT_EQ(metrics.publishedSnapshotCount, 1u);
        EXPECT_EQ(metrics.suppressedCatchUpSnapshotCount, 3u);

        const std::span<const WorldTickSample> samples = sampleBuffer->Samples();
        ASSERT_EQ(samples.size(), 1);
        EXPECT_EQ(samples[0].epoch, 1u);
        EXPECT_EQ(samples[0].firstServerTick, 100u);
        EXPECT_EQ(samples[0].lastServerTick, 103u);
        EXPECT_EQ(samples[0].processedTickCount, 4u);
        EXPECT_EQ(samples[0].dueTickCount, 5u);
        EXPECT_EQ(samples[0].startLagNanoseconds, 40000000u);
        EXPECT_EQ(samples[0].executionDurationNanoseconds, 0u);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, RecordsSamplesOnlyWhileRoundIsRunning)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 1, 0));

        FakeCoordinatorClock clock;
        clock.Advance(std::chrono::milliseconds{10});
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}};
        consumer.SetRoundState(WorldRoundRuntimeState{4, WorldRoundPhase::Waiting, 100, 0});
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> sampleBufferResult = WorldTickSampleBuffer::Create(2);
        ASSERT_TRUE(sampleBufferResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> sampleBuffer = sampleBufferResult.TakeValue();
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{10}, 1, 1, 100, 99, WorldClock::time_point{} + std::chrono::milliseconds{10},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager, nullptr, sampleBuffer.get(),
        };

        const WorldDoubleBufferedTickReport waitingReport = coordinator.RunNext(consumer, clock);
        ASSERT_EQ(waitingReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);

        consumer.SetRoundState(WorldRoundRuntimeState{4, WorldRoundPhase::Running, 200, 0});
        consumer.EndRoundAfterGameplayTick(101);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 2, 0));
        const WorldDoubleBufferedTickReport runningReport = coordinator.RunNext(consumer, clock);
        ASSERT_EQ(runningReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);

        ASSERT_EQ(consumer.GameplayState().RoundState().phase, WorldRoundPhase::Ended);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 3, 0));
        const WorldDoubleBufferedTickReport endedReport = coordinator.RunNext(consumer, clock);
        ASSERT_EQ(endedReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);

        const std::span<const WorldTickSample> samples = sampleBuffer->Samples();
        ASSERT_EQ(samples.size(), 1);
        EXPECT_EQ(samples[0].roundId, 4u);
        EXPECT_EQ(samples[0].roundPhase, WorldRoundPhase::Running);
        EXPECT_EQ(samples[0].firstServerTick, 101u);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, RecordsFollowingRoundAfterSampleBufferRebind)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 1, 0));

        FakeCoordinatorClock clock;
        clock.Advance(std::chrono::milliseconds{10});
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}};
        consumer.SetRoundState(WorldRoundRuntimeState{4, WorldRoundPhase::Running, 100, 0});
        consumer.EndRoundAfterGameplayTick(100);
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> firstBufferResult = WorldTickSampleBuffer::Create(1);
        ASSERT_TRUE(firstBufferResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> firstBuffer = firstBufferResult.TakeValue();
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{10}, 1, 1, 100, 99, WorldClock::time_point{} + std::chrono::milliseconds{10},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager, nullptr, firstBuffer.get(),
        };

        const WorldDoubleBufferedTickReport firstRoundReport = coordinator.RunNext(consumer, clock);
        ASSERT_EQ(firstRoundReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        ASSERT_EQ(firstBuffer->SampleCount(), 1u);
        EXPECT_EQ(firstBuffer->Samples()[0].roundId, 4u);

        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> secondBufferResult = WorldTickSampleBuffer::Create(1);
        ASSERT_TRUE(secondBufferResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> secondBuffer = secondBufferResult.TakeValue();
        coordinator.BindTickSampleBuffer(secondBuffer.get());
        consumer.SetRoundState(WorldRoundRuntimeState{5, WorldRoundPhase::Running, 101, 0});
        consumer.EndRoundAfterGameplayTick(101);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 2, 0));

        const WorldDoubleBufferedTickReport secondRoundReport = coordinator.RunNext(consumer, clock);

        ASSERT_EQ(secondRoundReport.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        ASSERT_EQ(secondBuffer->SampleCount(), 1u);
        EXPECT_EQ(secondBuffer->Samples()[0].roundId, 5u);
        EXPECT_EQ(secondBuffer->Samples()[0].roundPhase, WorldRoundPhase::Running);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, SkipsSimulationButKeepsDurableTickProgress)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 1, 0));

        FakeCoordinatorClock clock;
        clock.Advance(std::chrono::milliseconds{40});
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}};
        consumer.StopSimulationAfterGameplayTick(100);
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{10}, 4, 1, 100, 99, WorldClock::time_point{} + std::chrono::milliseconds{10},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager,
        };

        const WorldDoubleBufferedTickReport report = coordinator.RunNext(consumer, clock);

        EXPECT_EQ(report.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        EXPECT_EQ(report.processedTickCount, 4u);
        ASSERT_EQ(consumer.GameplayTickCount(), 1u);
        EXPECT_EQ(consumer.GameplayServerTick(0), 100u);
        EXPECT_EQ(consumer.DurableTickCount(), 4u);
        EXPECT_EQ(consumer.PublishFirstServerTick(), 100u);
        EXPECT_EQ(consumer.PublishLastServerTick(), 103u);
        EXPECT_EQ(coordinator.LastCompletedServerTick(), 103u);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, StopsCatchUpAfterFirstFailedTick)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*buffer, 1, 0));

        FakeCoordinatorClock clock;
        clock.Advance(std::chrono::milliseconds{40});
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}};
        consumer.FailGameplayAt(101);
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{10}, 4, 1, 100, 99, WorldClock::time_point{} + std::chrono::milliseconds{10},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager,
        };

        const WorldDoubleBufferedTickReport report = coordinator.RunNext(consumer, clock);

        EXPECT_EQ(report.stopReason, WorldDoubleBufferedTickStopReason::TickProcessFailed);
        EXPECT_EQ(report.tickProcessResult, WorldTickProcessResult::GameplayProcessFailed);
        EXPECT_EQ(report.processedTickCount, 1u);
        ASSERT_EQ(consumer.GameplayTickCount(), 2u);
        EXPECT_EQ(consumer.GameplayServerTick(0), 100u);
        EXPECT_EQ(consumer.GameplayServerTick(1), 101u);
        EXPECT_EQ(consumer.DurableTickCount(), 1u);
        EXPECT_EQ(consumer.PublishCallCount(), 0u);
        EXPECT_EQ(coordinator.LastCompletedServerTick(), 100u);
        EXPECT_EQ(coordinator.Metrics().processedTickCount, 1u);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, MissingSealedReadKeepsCurrentPlan)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateCoordinatorBuffer();
        ASSERT_NE(buffer, nullptr);
        FakeCoordinatorClock clock;
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}};
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{50}, 1, 7, 200, 199, WorldClock::time_point{},
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *buffer, sessionRegistry, commandStore, entityManager,
        };

        const WorldDoubleBufferedTickReport report = coordinator.RunNext(consumer, clock);

        EXPECT_EQ(report.stopReason, WorldDoubleBufferedTickStopReason::IngressAcquireFailed);
        EXPECT_EQ(report.exchangeResult, WorldIngressDoubleBufferExchangeResult::TimedOut);
        EXPECT_EQ(coordinator.NextPlan().epoch, 7u);
        EXPECT_EQ(coordinator.NextPlan().serverTick, 200u);
        EXPECT_EQ(coordinator.Metrics().processedTickCount, 0u);
    }

    TEST(WorldDoubleBufferedTickCoordinatorTests, SealsDoubleBufferedOutboundAtTickBoundary)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> ingressBuffer = CreateCoordinatorBuffer();
        ASSERT_NE(ingressBuffer, nullptr);
        ASSERT_TRUE(PublishIngressEpoch(*ingressBuffer, 1, 0));
        WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> outboundBufferResult =
            WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{2, 2, 8});
        ASSERT_TRUE(outboundBufferResult.Succeeded());
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer = outboundBufferResult.TakeValue();
        ASSERT_NE(outboundBuffer, nullptr);

        FakeCoordinatorClock clock;
        FakeCoordinatorEventConsumer consumer{clock, std::chrono::milliseconds{0}, outboundBuffer.get()};
        WorldSessionRegistry sessionRegistry;
        WorldMovementCommandStore commandStore;
        WorldEntityManager entityManager;
        const WorldDoubleBufferedTickConfig config{
            std::chrono::milliseconds{50}, 1, 1, 100, 99, WorldClock::time_point{}, WorldOutboundMode::DoubleBuffered,
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            config, *ingressBuffer, sessionRegistry, commandStore, entityManager, outboundBuffer.get(),
        };

        const WorldDoubleBufferedTickReport report = coordinator.RunNext(consumer, clock);

        EXPECT_EQ(report.stopReason, WorldDoubleBufferedTickStopReason::Completed);
        EXPECT_EQ(report.outboundExchangeResult, WorldOutboundDoubleBufferExchangeResult::Exchanged);
        WorldOutboundReadBatch outboundBatch;
        ASSERT_EQ(outboundBuffer->WaitAcquireRead(std::chrono::milliseconds{0}, &outboundBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(outboundBatch.epoch, 1u);
        EXPECT_EQ(outboundBatch.firstServerTick, 100u);
        EXPECT_EQ(outboundBatch.lastServerTick, 100u);
        EXPECT_EQ(outboundBatch.records.size(), 1u);
        EXPECT_EQ(outboundBuffer->ReleaseRead(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }
} // namespace psnr::world::tests
