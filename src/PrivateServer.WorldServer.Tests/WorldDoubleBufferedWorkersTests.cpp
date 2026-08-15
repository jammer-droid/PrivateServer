#include "pch.h"

#include "WorldApplicationEventSinkTestDouble.h"
#include "WorldDoubleBufferedWorkers.h"
#include "WorldWorkerShutdown.h"
#include "WorldWorkerStartup.h"

#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerConfig.h>

#include <array>
#include <chrono>
#include <future>
#include <memory>

namespace psnr::world::tests
{
    namespace
    {
        WorldApplicationEventSinkTestDouble applicationEventSink;

        [[nodiscard]] std::unique_ptr<WorldIngressDoubleBuffer> CreateIngressBuffer()
        {
            WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> result = WorldIngressDoubleBuffer::Create(4);
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] std::unique_ptr<WorldOutboundDoubleBuffer> CreateOutboundBuffer()
        {
            WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> result =
                WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{4, 4, 64});
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] std::unique_ptr<WorldTickSampleBuffer> CreateTickSampleBuffer(const std::size_t capacity)
        {
            WorldResult<std::unique_ptr<WorldTickSampleBuffer>> result = WorldTickSampleBuffer::Create(capacity);
            return result.Failed() ? nullptr : result.TakeValue();
        }

        class TestRuntimeShutdown final
        {
        public:
            explicit TestRuntimeShutdown(psnr::runtime::NrServer& server) noexcept
                : server_(server)
            {
            }

            [[nodiscard]] bool RequestStopAndShutdown() noexcept
            {
                return server_.RequestStop().Succeeded() && server_.Shutdown().Succeeded();
            }

        private:
            psnr::runtime::NrServer& server_;
        };
    } // namespace

    TEST(WorldDoubleBufferedWorkersTests, ExchangesOneTerminalPlanForMatchingPumpCompletion)
    {
        WorldIngressWorkerExchange exchange;
        const WorldClock::time_point deadline = WorldClock::now() + std::chrono::milliseconds{10};

        ASSERT_EQ(exchange.Publish(WorldIngressWorkerPlan{7, deadline}), WorldIngressWorkerExchangeResult::Exchanged);

        WorldIngressWorkerPlan plan;
        ASSERT_EQ(exchange.WaitTakePlan(&plan), WorldIngressWorkerExchangeResult::Exchanged);
        EXPECT_EQ(plan.epoch, 7u);
        EXPECT_EQ(plan.deadline, deadline);

        WorldIngressWorkerCompletion completion;
        completion.epoch = 7;
        completion.terminalReport.stopReason = WorldIngressTerminalEpochStopReason::SealedFull;
        ASSERT_EQ(exchange.Complete(completion), WorldIngressWorkerExchangeResult::Exchanged);

        WorldIngressWorkerCompletion receivedCompletion;
        ASSERT_EQ(exchange.WaitTakeCompletion(7, &receivedCompletion), WorldIngressWorkerExchangeResult::Exchanged);
        EXPECT_EQ(receivedCompletion.terminalReport.stopReason, WorldIngressTerminalEpochStopReason::SealedFull);
    }

    TEST(WorldDoubleBufferedWorkersTests, CloseWakesPlanWaiter)
    {
        WorldIngressWorkerExchange exchange;
        std::future<WorldIngressWorkerExchangeResult> waitResult = std::async(std::launch::async,
                                                                              [&exchange]()
                                                                              {
                                                                                  WorldIngressWorkerPlan plan;
                                                                                  return exchange.WaitTakePlan(&plan);
                                                                              });

        exchange.Close();

        EXPECT_EQ(waitResult.get(), WorldIngressWorkerExchangeResult::Closed);
    }

    TEST(WorldDoubleBufferedWorkersTests, PublisherWorkerDrainWakesBufferWaitAndJoins)
    {
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer();
        ASSERT_NE(buffer, nullptr);
        psnr::runtime::NrGateway gateway;
        WorldOutboundPublisher publisher{*buffer};
        WorldOutboundPublisherWorker worker{publisher, gateway};

        ASSERT_TRUE(worker.Start());
        EXPECT_TRUE(worker.DrainAndStop());
        EXPECT_EQ(worker.StopReason(), WorldConcreteWorkerStopReason::Completed);
        EXPECT_EQ(worker.LastReport().epoch, 0u);
    }

    TEST(WorldDoubleBufferedWorkersTests, PublisherStopWakesCoordinatorBlockedAtOutboundPrepare)
    {
        constexpr std::chrono::milliseconds WaitTimeout{500};
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer();
        ASSERT_NE(buffer, nullptr);
        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{};
        const std::array<std::byte, 1> payload{std::byte{1}};
        ASSERT_EQ(buffer->BeginWriteBatch(1, 1, 1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{1}, recipients, payload),
                  WorldOutboundAppendResult::Appended);
        ASSERT_EQ(buffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds::zero(), &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->BeginWriteBatch(2, 2, 2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(buffer->TryAppend(psnr::core::NrPacketType{2}, recipients, payload),
                  WorldOutboundAppendResult::Appended);

        ASSERT_EQ(buffer->SealWrite(2), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        std::future<WorldOutboundDoubleBufferExchangeResult> prepareResult =
            std::async(std::launch::async, [&buffer, WaitTimeout]() { return buffer->WaitPrepareWrite(WaitTimeout); });
        ASSERT_EQ(prepareResult.wait_for(std::chrono::milliseconds{20}), std::future_status::timeout);

        psnr::runtime::NrGateway gateway;
        WorldOutboundPublisher publisher{*buffer};
        WorldOutboundPublisherWorker worker{publisher, gateway};
        worker.RequestStop();

        EXPECT_EQ(prepareResult.get(), WorldOutboundDoubleBufferExchangeResult::InvalidState);
        EXPECT_EQ(buffer->ReleaseRead(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);
    }

    TEST(WorldDoubleBufferedWorkersTests, PumpWorkerStopWakesPlanWaitAndJoins)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateIngressBuffer();
        ASSERT_NE(buffer, nullptr);
        psnr::runtime::NrServer server;
        NrServerWorldEventSource source{server};
        WorldIngressPump pump{*buffer};
        WorldIngressWorkerExchange exchange;
        WorldIngressPumpWorker worker{pump, source, exchange};

        ASSERT_TRUE(worker.Start());
        worker.RequestStop();
        worker.Join();

        EXPECT_EQ(worker.StopReason(), WorldConcreteWorkerStopReason::StopRequested);
    }

    TEST(WorldDoubleBufferedWorkersTests, CoordinatorWorkerRejectsNegativeLifecycleTimeout)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> buffer = CreateIngressBuffer();
        ASSERT_NE(buffer, nullptr);
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementCommandStore;
        WorldIngressEventConsumer eventConsumer{sessionRegistry,
                                                entityManager,
                                                movementCommandStore,
                                                server,
                                                gateway,
                                                WorldIngressEventConsumerConfig{},
                                                1,
                                                0,
                                                applicationEventSink};
        WorldIngressWorkerExchange exchange;
        const WorldDoubleBufferedTickConfig tickConfig{
            std::chrono::milliseconds{10}, 1, 1, 1, 0, WorldClock::now() + std::chrono::milliseconds{10},
            WorldOutboundMode::Direct,
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            tickConfig, *buffer, sessionRegistry, movementCommandStore, entityManager,
        };
        WorldDoubleBufferedCoordinatorWorker worker{
            WorldDoubleBufferedCoordinatorWorkerConfig{std::chrono::milliseconds{-1}},
            coordinator,
            *buffer,
            eventConsumer,
            exchange,
        };

        EXPECT_FALSE(worker.Start());
        EXPECT_EQ(worker.StopReason(), WorldConcreteWorkerStopReason::NotStarted);
    }

    TEST(WorldDoubleBufferedWorkersTests, StartupPolicyUsesConcreteWorkersAndRollsBackCoordinatorFailure)
    {
        std::unique_ptr<WorldIngressDoubleBuffer> ingressBuffer = CreateIngressBuffer();
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer = CreateOutboundBuffer();
        ASSERT_NE(ingressBuffer, nullptr);
        ASSERT_NE(outboundBuffer, nullptr);

        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        NrServerWorldEventSource source{server};
        WorldIngressPump pump{*ingressBuffer};
        WorldOutboundPublisher publisher{*outboundBuffer};
        WorldIngressWorkerExchange ingressExchange;
        WorldIngressPumpWorker pumpWorker{pump, source, ingressExchange};
        WorldOutboundPublisherWorker publisherWorker{publisher, gateway};

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementCommandStore;
        WorldIngressEventConsumer eventConsumer{sessionRegistry,
                                                entityManager,
                                                movementCommandStore,
                                                server,
                                                gateway,
                                                WorldOutboundMode::DoubleBuffered,
                                                outboundBuffer.get(),
                                                WorldIngressEventConsumerConfig{},
                                                1,
                                                0,
                                                applicationEventSink};
        const WorldDoubleBufferedTickConfig tickConfig{
            std::chrono::milliseconds{10},     1, 1, 1, 0, WorldClock::now() + std::chrono::milliseconds{10},
            WorldOutboundMode::DoubleBuffered,
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            tickConfig, *ingressBuffer, sessionRegistry, movementCommandStore, entityManager, outboundBuffer.get(),
        };
        WorldDoubleBufferedCoordinatorWorker coordinatorWorker{
            WorldDoubleBufferedCoordinatorWorkerConfig{std::chrono::milliseconds{-1}},
            coordinator,
            *ingressBuffer,
            eventConsumer,
            ingressExchange,
        };

        const WorldWorkerStartupReport report = WorldWorkerStartup::Start(
            WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered},
            &publisherWorker, &pumpWorker, coordinatorWorker);

        EXPECT_EQ(report.result, WorldWorkerStartupResult::WorkerStartFailed);
        EXPECT_EQ(report.failedWorker, WorldWorkerKind::Coordinator);
        EXPECT_TRUE(report.rollbackCompleted);
        EXPECT_EQ(publisherWorker.StopReason(), WorldConcreteWorkerStopReason::StopRequested);
        EXPECT_EQ(pumpWorker.StopReason(), WorldConcreteWorkerStopReason::StopRequested);
    }

    TEST(WorldDoubleBufferedWorkersTests, PublicRuntimeIdleSmokeStartsTicksAndShutsDownAllWorkers)
    {
        psnr::runtime::NrServerConfig runtimeConfig;
        runtimeConfig.bindEndpoint.port = 37941;
        psnr::runtime::NrServer server;
        ASSERT_TRUE(psnr::runtime::NrServer::Create(runtimeConfig, &server).Succeeded());
        ASSERT_TRUE(server.Start().Succeeded());
        psnr::runtime::NrGateway gateway;
        ASSERT_TRUE(server.CreateGateway(&gateway).Succeeded());

        std::unique_ptr<WorldIngressDoubleBuffer> ingressBuffer = CreateIngressBuffer();
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer = CreateOutboundBuffer();
        ASSERT_NE(ingressBuffer, nullptr);
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementCommandStore;
        NrServerWorldEventSource source{server};
        WorldIngressEventConsumer eventConsumer{sessionRegistry,
                                                entityManager,
                                                movementCommandStore,
                                                server,
                                                gateway,
                                                WorldOutboundMode::DoubleBuffered,
                                                outboundBuffer.get(),
                                                WorldIngressEventConsumerConfig{},
                                                1,
                                                0,
                                                applicationEventSink};
        WorldIngressPump pump{*ingressBuffer};
        WorldOutboundPublisher publisher{*outboundBuffer};
        WorldIngressWorkerExchange ingressExchange;
        const std::chrono::milliseconds fixedStep{50};
        const WorldDoubleBufferedTickConfig tickConfig{
            fixedStep, 1, 1, 1, 0, WorldClock::now() + fixedStep, WorldOutboundMode::DoubleBuffered,
        };
        WorldDoubleBufferedTickCoordinator coordinator{
            tickConfig, *ingressBuffer, sessionRegistry, movementCommandStore, entityManager, outboundBuffer.get(),
        };
        WorldOutboundPublisherWorker publisherWorker{publisher, gateway};
        WorldIngressPumpWorker pumpWorker{pump, source, ingressExchange};
        WorldDoubleBufferedCoordinatorWorker coordinatorWorker{
            WorldDoubleBufferedCoordinatorWorkerConfig{std::chrono::milliseconds{500}},
            coordinator,
            *ingressBuffer,
            eventConsumer,
            ingressExchange,
        };
        const WorldExecutionModeConfig modes{
            WorldInboundMode::DoubleBuffered,
            WorldOutboundMode::DoubleBuffered,
        };
        ASSERT_EQ(WorldWorkerStartup::Start(modes, &publisherWorker, &pumpWorker, coordinatorWorker).result,
                  WorldWorkerStartupResult::Started);

        std::this_thread::sleep_for(std::chrono::milliseconds{160});

        TestRuntimeShutdown runtime{server};
        const WorldWorkerShutdownReport shutdownReport =
            WorldWorkerShutdown::Run(modes, runtime, &publisherWorker, &pumpWorker, coordinatorWorker);

        EXPECT_EQ(shutdownReport.result, WorldWorkerShutdownResult::Completed);
        EXPECT_GT(coordinator.Metrics().processedTickCount, 0u);
        EXPECT_EQ(publisherWorker.StopReason(), WorldConcreteWorkerStopReason::Completed);
        EXPECT_EQ(pumpWorker.StopReason(), WorldConcreteWorkerStopReason::Completed);
        EXPECT_EQ(coordinatorWorker.StopReason(), WorldConcreteWorkerStopReason::Completed);

        EXPECT_EQ(coordinatorWorker.TickSampleCollectionFailureCount(), 0u);
    }
} // namespace psnr::world::tests
