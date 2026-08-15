#include "pch.h"

#include "NrServerWorldEventSource.h"
#include "WorldIngressEventDrain.h"

namespace psnr::world::tests
{
    namespace
    {
        class ScriptedWorldEventSource final
        {
        public:
            explicit ScriptedWorldEventSource(const std::size_t eventCount,
                                              const psnr::core::NrStatus terminalStatus) noexcept
                : remainingEventCount_(eventCount)
                , terminalStatus_(terminalStatus)
            {
            }

            [[nodiscard]] psnr::core::NrStatus TryPopBatch(psnr::runtime::NrToWorldEvent* const eventBuffer,
                                                           const std::size_t eventBufferCount,
                                                           std::size_t* const outEventCount) noexcept
            {
                ++batchPopCallCount_;
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

            [[nodiscard]] std::size_t RemainingEventCount() const noexcept
            {
                return remainingEventCount_;
            }

            [[nodiscard]] std::size_t BatchPopCallCount() const noexcept
            {
                return batchPopCallCount_;
            }

        private:
            std::size_t remainingEventCount_ = 0;
            std::size_t batchPopCallCount_ = 0;
            psnr::core::NrStatus terminalStatus_{};
        };

        class CountingWorldEventConsumer final
        {
        public:
            void Handle(const psnr::runtime::NrToWorldEvent&)
            {
                ++handledEventCount_;
            }

            [[nodiscard]] std::size_t HandledEventCount() const noexcept
            {
                return handledEventCount_;
            }

        private:
            std::size_t handledEventCount_ = 0;
        };
    } // namespace

    TEST(WorldIngressEventDrainTests, StopsAtBudgetAndLeavesRemainingEventsForNextDrain)
    {
        ScriptedWorldEventSource source{
            3,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        CountingWorldEventConsumer consumer;

        const WorldIngressDrainReport firstReport = WorldIngressEventDrain::Drain(source, consumer, 2);

        EXPECT_EQ(firstReport.stopReason, WorldIngressDrainStopReason::BudgetExhausted);
        EXPECT_EQ(firstReport.drainedEventCount, 2u);
        EXPECT_TRUE(firstReport.sourceStatus.Succeeded());
        EXPECT_EQ(source.RemainingEventCount(), 1u);
        EXPECT_EQ(source.BatchPopCallCount(), 1u);
        EXPECT_EQ(consumer.HandledEventCount(), 2u);

        const WorldIngressDrainReport secondReport = WorldIngressEventDrain::Drain(source, consumer, 2);

        EXPECT_EQ(secondReport.stopReason, WorldIngressDrainStopReason::QueueEmpty);
        EXPECT_EQ(secondReport.drainedEventCount, 1u);
        EXPECT_EQ(secondReport.sourceStatus.ErrorCode(), psnr::core::NrErrorCode::QueueEmpty);
        EXPECT_EQ(source.RemainingEventCount(), 0u);
        EXPECT_EQ(source.BatchPopCallCount(), 2u);
        EXPECT_EQ(consumer.HandledEventCount(), 3u);
    }

    TEST(WorldIngressEventDrainTests, EmptyBudgetDoesNotPopOrHandleEvents)
    {
        ScriptedWorldEventSource source{
            1,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueEmpty),
        };
        CountingWorldEventConsumer consumer;

        const WorldIngressDrainReport report = WorldIngressEventDrain::Drain(source, consumer, 0);

        EXPECT_EQ(report.stopReason, WorldIngressDrainStopReason::BudgetExhausted);
        EXPECT_EQ(report.drainedEventCount, 0u);
        EXPECT_EQ(source.RemainingEventCount(), 1u);
        EXPECT_EQ(source.BatchPopCallCount(), 0u);
        EXPECT_EQ(consumer.HandledEventCount(), 0u);
    }

    TEST(WorldIngressEventDrainTests, PreservesUnexpectedRuntimeFailureInReport)
    {
        ScriptedWorldEventSource source{
            1,
            psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 123),
        };
        CountingWorldEventConsumer consumer;

        const WorldIngressDrainReport firstReport = WorldIngressEventDrain::Drain(source, consumer, 1);
        const WorldIngressDrainReport secondReport = WorldIngressEventDrain::Drain(source, consumer, 1);

        EXPECT_EQ(firstReport.stopReason, WorldIngressDrainStopReason::BudgetExhausted);
        EXPECT_EQ(firstReport.drainedEventCount, 1u);
        EXPECT_EQ(secondReport.stopReason, WorldIngressDrainStopReason::SourceFailure);
        EXPECT_EQ(secondReport.drainedEventCount, 0u);
        EXPECT_EQ(secondReport.sourceStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        EXPECT_EQ(secondReport.sourceStatus.NativeErrorCode(), 123u);
        EXPECT_EQ(consumer.HandledEventCount(), 1u);
    }

    TEST(WorldIngressEventDrainTests, NrServerAdapterForwardsPublicRuntimeFailure)
    {
        psnr::runtime::NrServer server;
        NrServerWorldEventSource source{server};
        CountingWorldEventConsumer consumer;
        psnr::runtime::NrToWorldWaitResult waitResult = psnr::runtime::NrToWorldWaitResult::EventsAvailable;

        const WorldIngressDrainReport report = WorldIngressEventDrain::Drain(source, consumer, 1);
        const psnr::core::NrStatus waitStatus = source.WaitForEvents(0, &waitResult);

        EXPECT_EQ(report.stopReason, WorldIngressDrainStopReason::SourceFailure);
        EXPECT_EQ(report.drainedEventCount, 0u);
        EXPECT_EQ(report.sourceStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        EXPECT_EQ(waitStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        EXPECT_EQ(waitResult, psnr::runtime::NrToWorldWaitResult::EventsAvailable);
        EXPECT_EQ(consumer.HandledEventCount(), 0u);
    }
} // namespace psnr::world::tests
