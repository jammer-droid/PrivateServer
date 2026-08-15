#include "pch.h"

#include "WorldWorkerShutdown.h"

#include <string>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        class FakeShutdownRuntime final
        {
        public:
            FakeShutdownRuntime(std::vector<std::string>& calls, const bool result = true) noexcept
                : calls_(calls)
                , result_(result)
            {
            }

            [[nodiscard]] bool RequestStopAndShutdown()
            {
                calls_.push_back("runtime:shutdown");
                return result_;
            }

        private:
            std::vector<std::string>& calls_;
            bool result_ = true;
        };

        class FakeShutdownPublisher final
        {
        public:
            FakeShutdownPublisher(std::vector<std::string>& calls, const bool result = true) noexcept
                : calls_(calls)
                , result_(result)
            {
            }

            [[nodiscard]] bool DrainAndStop()
            {
                calls_.push_back("publisher:drain-stop");
                return result_;
            }

        private:
            std::vector<std::string>& calls_;
            bool result_ = true;
        };

        class FakeShutdownPump final
        {
        public:
            explicit FakeShutdownPump(std::vector<std::string>& calls,
                                      const bool terminalDrainSucceeded = true) noexcept
                : calls_(calls)
                , terminalDrainSucceeded_(terminalDrainSucceeded)
            {
            }

            void EnterTerminalDrain()
            {
                calls_.push_back("pump:terminal");
            }

            void Join()
            {
                calls_.push_back("pump:join");
            }

            [[nodiscard]] bool TerminalDrainSucceeded() const noexcept
            {
                return terminalDrainSucceeded_;
            }

        private:
            std::vector<std::string>& calls_;
            bool terminalDrainSucceeded_ = true;
        };

        class FakeShutdownCoordinator final
        {
        public:
            explicit FakeShutdownCoordinator(std::vector<std::string>& calls,
                                             const bool terminalConsumeSucceeded = true,
                                             const bool gameplayStopSucceeded = true) noexcept
                : calls_(calls)
                , terminalConsumeSucceeded_(terminalConsumeSucceeded)
                , gameplayStopSucceeded_(gameplayStopSucceeded)
            {
            }

            [[nodiscard]] bool StopGameplayAndWait()
            {
                calls_.push_back("coordinator:stop-gameplay");
                return gameplayStopSucceeded_;
            }

            void EnterTerminalConsume()
            {
                calls_.push_back("coordinator:terminal");
            }

            void Join()
            {
                calls_.push_back("coordinator:join");
            }

            [[nodiscard]] bool TerminalConsumeSucceeded() const noexcept
            {
                return terminalConsumeSucceeded_;
            }

            [[nodiscard]] WorldIngressTerminalConsumeReport TerminalConsumeReport() const noexcept
            {
                return WorldIngressTerminalConsumeReport{1, 2, 3, 4};
            }

        private:
            std::vector<std::string>& calls_;
            bool terminalConsumeSucceeded_ = true;
            bool gameplayStopSucceeded_ = true;
        };
    } // namespace

    TEST(WorldWorkerShutdownTests, DrainsOutboundBeforeTerminalIngressAndRuntimeShutdown)
    {
        std::vector<std::string> calls;
        FakeShutdownRuntime runtime{calls};
        FakeShutdownPublisher publisher{calls};
        FakeShutdownPump pump{calls};
        FakeShutdownCoordinator coordinator{calls};

        const WorldWorkerShutdownReport report = WorldWorkerShutdown::Run(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, runtime,
            &publisher, &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerShutdownResult::Completed);
        EXPECT_TRUE(report.gameplayStopped);
        EXPECT_TRUE(report.outboundDrained);
        EXPECT_TRUE(report.terminalIngressEnabled);
        EXPECT_TRUE(report.terminalIngressDrained);
        EXPECT_TRUE(report.runtimeShutdownRequested);
        EXPECT_TRUE(report.runtimeShutdownSucceeded);
        EXPECT_TRUE(report.workersJoined);
        EXPECT_EQ(report.terminalIngress.acceptedEventCount, 1u);
        EXPECT_EQ(report.terminalIngress.closedEventCount, 2u);
        EXPECT_EQ(report.terminalIngress.discardedPacketCount, 3u);
        EXPECT_EQ(report.terminalIngress.unsupportedEventCount, 4u);
        EXPECT_EQ(calls, (std::vector<std::string>{
                             "coordinator:stop-gameplay",
                             "publisher:drain-stop",
                             "coordinator:terminal",
                             "pump:terminal",
                             "runtime:shutdown",
                             "pump:join",
                             "coordinator:join",
                         }));
    }

    TEST(WorldWorkerShutdownTests, OutboundDrainFailureStillCompletesRuntimeAndWorkerCleanup)
    {
        std::vector<std::string> calls;
        FakeShutdownRuntime runtime{calls};
        FakeShutdownPublisher publisher{calls, false};
        FakeShutdownPump pump{calls};
        FakeShutdownCoordinator coordinator{calls};

        const WorldWorkerShutdownReport report = WorldWorkerShutdown::Run(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, runtime,
            &publisher, &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerShutdownResult::OutboundDrainFailed);
        EXPECT_FALSE(report.outboundDrained);
        EXPECT_TRUE(report.terminalIngressDrained);
        EXPECT_TRUE(report.runtimeShutdownSucceeded);
        EXPECT_TRUE(report.workersJoined);
        EXPECT_EQ(calls.back(), "coordinator:join");
    }

    TEST(WorldWorkerShutdownTests, TerminalDrainFailureIsReportedAfterAllWorkersJoin)
    {
        std::vector<std::string> calls;
        FakeShutdownRuntime runtime{calls};
        FakeShutdownPublisher publisher{calls};
        FakeShutdownPump pump{calls, false};
        FakeShutdownCoordinator coordinator{calls};

        const WorldWorkerShutdownReport report = WorldWorkerShutdown::Run(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, runtime,
            &publisher, &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerShutdownResult::TerminalIngressDrainFailed);
        EXPECT_FALSE(report.terminalIngressDrained);
        EXPECT_TRUE(report.workersJoined);
        EXPECT_EQ(calls.back(), "coordinator:join");
    }

    TEST(WorldWorkerShutdownTests, GameplayStopTimeoutIsReportedAfterCleanupContinues)
    {
        std::vector<std::string> calls;
        FakeShutdownRuntime runtime{calls};
        FakeShutdownPublisher publisher{calls};
        FakeShutdownPump pump{calls};
        FakeShutdownCoordinator coordinator{calls, true, false};

        const WorldWorkerShutdownReport report = WorldWorkerShutdown::Run(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, runtime,
            &publisher, &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerShutdownResult::GameplayStopTimedOut);
        EXPECT_FALSE(report.gameplayStopped);
        EXPECT_TRUE(report.workersJoined);
        EXPECT_EQ(calls.back(), "coordinator:join");
    }

    TEST(WorldWorkerShutdownTests, RuntimeShutdownFailureIsReportedAfterTerminalDrainAndWorkerJoin)
    {
        std::vector<std::string> calls;
        FakeShutdownRuntime runtime{calls, false};
        FakeShutdownPublisher publisher{calls};
        FakeShutdownPump pump{calls};
        FakeShutdownCoordinator coordinator{calls};

        const WorldWorkerShutdownReport report = WorldWorkerShutdown::Run(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, runtime,
            &publisher, &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerShutdownResult::RuntimeShutdownFailed);
        EXPECT_TRUE(report.runtimeShutdownRequested);
        EXPECT_FALSE(report.runtimeShutdownSucceeded);
        EXPECT_TRUE(report.terminalIngressDrained);
        EXPECT_TRUE(report.workersJoined);
        EXPECT_EQ(calls.back(), "coordinator:join");
    }
} // namespace psnr::world::tests
