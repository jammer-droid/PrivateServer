#include "pch.h"

#include "WorldWorkerStartup.h"

#include <string>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        class FakeStartupWorker final
        {
        public:
            FakeStartupWorker(std::string name, std::vector<std::string>& calls, const bool startResult = true)
                : name_(std::move(name))
                , calls_(calls)
                , startResult_(startResult)
            {
            }

            [[nodiscard]] bool Start()
            {
                calls_.push_back("start:" + name_);
                return startResult_;
            }

            void RequestStop()
            {
                calls_.push_back("stop:" + name_);
            }

            void Join()
            {
                calls_.push_back("join:" + name_);
            }

        private:
            std::string name_;
            std::vector<std::string>& calls_;
            bool startResult_ = true;
        };
    } // namespace

    TEST(WorldWorkerStartupTests, StartsSelectedWorkersInDependencyOrder)
    {
        std::vector<std::string> calls;
        FakeStartupWorker publisher{"publisher", calls};
        FakeStartupWorker pump{"pump", calls};
        FakeStartupWorker coordinator{"coordinator", calls};

        const WorldWorkerStartupReport report = WorldWorkerStartup::Start(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, &publisher,
            &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerStartupResult::Started);
        EXPECT_TRUE(report.outboundPublisherStarted);
        EXPECT_TRUE(report.ingressPumpStarted);
        EXPECT_TRUE(report.coordinatorStarted);
        EXPECT_EQ(calls, (std::vector<std::string>{"start:publisher", "start:pump", "start:coordinator"}));
    }

    TEST(WorldWorkerStartupTests, CoordinatorFailureRollsBackStartedWorkersInReverseOrder)
    {
        std::vector<std::string> calls;
        FakeStartupWorker publisher{"publisher", calls};
        FakeStartupWorker pump{"pump", calls};
        FakeStartupWorker coordinator{"coordinator", calls, false};

        const WorldWorkerStartupReport report = WorldWorkerStartup::Start(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered}, &publisher,
            &pump, coordinator);

        EXPECT_EQ(report.result, WorldWorkerStartupResult::WorkerStartFailed);
        EXPECT_EQ(report.failedWorker, WorldWorkerKind::Coordinator);
        EXPECT_TRUE(report.rollbackCompleted);
        EXPECT_EQ(calls, (std::vector<std::string>{
                             "start:publisher",
                             "start:pump",
                             "start:coordinator",
                             "stop:pump",
                             "stop:publisher",
                             "join:pump",
                             "join:publisher",
                         }));
    }

    TEST(WorldWorkerStartupTests, DirectTargetModeStartsOnlyCoordinator)
    {
        std::vector<std::string> calls;
        FakeStartupWorker publisher{"publisher", calls};
        FakeStartupWorker pump{"pump", calls};
        FakeStartupWorker coordinator{"coordinator", calls};

        const WorldWorkerStartupReport report = WorldWorkerStartup::Start(
            WorldExecutionModeConfig{WorldInboundMode::TargetServerTick, WorldOutboundMode::Direct}, &publisher, &pump,
            coordinator);

        EXPECT_EQ(report.result, WorldWorkerStartupResult::Started);
        EXPECT_FALSE(report.outboundPublisherStarted);
        EXPECT_FALSE(report.ingressPumpStarted);
        EXPECT_TRUE(report.coordinatorStarted);
        EXPECT_EQ(calls, (std::vector<std::string>{"start:coordinator"}));
    }
} // namespace psnr::world::tests
