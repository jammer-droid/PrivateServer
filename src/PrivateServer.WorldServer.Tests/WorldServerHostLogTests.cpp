#include "pch.h"

#include "ApplicationLogFanoutSink.h"
#include "ApplicationLogger.h"
#include "ApplicationLoggerTestAccess.h"
#include "WorldServerHostLog.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        struct RecordingOutputState final
        {
            mutable std::mutex mutex;
            std::vector<std::string> payloads;
        };

        class RecordingOutput final : public psnr::logging::internal::IApplicationLogOutput
        {
        public:
            explicit RecordingOutput(RecordingOutputState* const state) noexcept
                : state_(state)
            {
            }

            void Write(const std::string_view payload) override
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->payloads.emplace_back(payload);
            }

            void Flush() override {}

        private:
            RecordingOutputState* state_;
        };

        [[nodiscard]] psnr::logging::ApplicationLogConfig ValidLogConfig()
        {
            psnr::logging::ApplicationLogConfig config{};
            config.runId = "run-20260806T120000.000Z-550e8400-e29b-41d4-a716-446655440000";
            config.process = "world_server_host_log_tests";
            config.outputDirectory = "unused-test-output";
            config.queueCapacity = 32;
            return config;
        }

        [[nodiscard]] bool ContainsPayload(const RecordingOutputState& state, const std::string_view token)
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            for (const std::string& payload : state.payloads)
            {
                if (payload.find(token) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::unique_ptr<psnr::logging::ApplicationLogger> CreateLogger(RecordingOutputState* const state)
        {
            std::unique_ptr<RecordingOutput> output = std::make_unique<RecordingOutput>(state);
            return psnr::logging::internal::ApplicationLoggerTestAccess::Create(ValidLogConfig(), std::move(output),
                                                                                nullptr);
        }
    } // namespace

    TEST(WorldServerHostLogTests, RecordsHostWorldRuntimeAndWorkerEvents)
    {
        RecordingOutputState outputState{};
        std::unique_ptr<psnr::logging::ApplicationLogger> logger = CreateLogger(&outputState);
        ASSERT_NE(logger, nullptr);
        ASSERT_EQ(logger->Start(), psnr::logging::ApplicationLoggerResult::Success);
        const host::WorldServerHostLog log{*logger, logger->CreateHandle()};

        log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "host_ready", "host_startup", "completed");
        log.WorldEvent(psnr::logging::ApplicationLogSeverity::Warning, "world_warning", "world_tick", "failed",
                       "tick_failure");
        log.RuntimeFailure("nr_server_start",
                           psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState, 123));
        log.NetworkListening(7, "Channel 7", {127, 0, 0, 1}, 7777);
        log.StorageCreateFailure(WorldErrorCode::AllocationFailed);
        log.WorldReady();
        log.WorkerStartupFailure(WorldWorkerKind::IngressPump);
        log.WorkerStoppedUnexpectedly(WorldWorkerKind::Coordinator, WorldConcreteWorkerStopReason::OperationFailed);
        logger->Shutdown();

        EXPECT_TRUE(ContainsPayload(outputState, "\"component\":\"host\",\"event\":\"host_ready\""));
        EXPECT_TRUE(ContainsPayload(outputState, "\"component\":\"world\",\"event\":\"world_warning\""));
        EXPECT_TRUE(ContainsPayload(outputState, "\"nativeErrorCode\":123"));
        EXPECT_TRUE(ContainsPayload(outputState, "channelId=7 channelName=Channel 7"));
        EXPECT_TRUE(ContainsPayload(outputState, "endpoint=127.0.0.1:7777"));
        EXPECT_TRUE(ContainsPayload(outputState, "\"error\":\"allocation_failed\""));
        EXPECT_TRUE(ContainsPayload(outputState, "inbound=double_buffered outbound=triple_buffered"));
        EXPECT_TRUE(ContainsPayload(outputState, "\"operation\":\"ingress_pump\""));
        EXPECT_TRUE(ContainsPayload(outputState, "\"error\":\"operation_failed\""));
    }

    TEST(WorldServerHostLogTests, RecordsShutdownMetricsAndTerminalHealth)
    {
        RecordingOutputState outputState{};
        std::unique_ptr<psnr::logging::ApplicationLogger> logger = CreateLogger(&outputState);
        ASSERT_NE(logger, nullptr);
        ASSERT_EQ(logger->Start(), psnr::logging::ApplicationLoggerResult::Success);
        const host::WorldServerHostLog log{*logger, logger->CreateHandle()};

        WorldWorkerShutdownReport shutdownReport{};
        shutdownReport.result = WorldWorkerShutdownResult::OutboundDrainFailed;
        shutdownReport.terminalIngress.acceptedEventCount = 3;
        WorldIngressPumpMetrics ingressMetrics{};
        ingressMetrics.drainedEventCount = 5;
        WorldDoubleBufferedTickMetrics tickMetrics{};
        tickMetrics.processedTickCount = 7;
        tickMetrics.maximumTickStartLagNanoseconds = 13;
        tickMetrics.maximumTickDurationNanoseconds = 17;
        WorldOutboundPublisherMetrics outboundMetrics{};
        outboundMetrics.publishedRecordCount = 11;
        outboundMetrics.publicationFailureCount = 2;

        log.RecordWorkerShutdown(shutdownReport, ingressMetrics, tickMetrics, outboundMetrics);
        EXPECT_EQ(log.Complete(1), 1);
        logger->Shutdown();

        EXPECT_TRUE(ContainsPayload(outputState, "\"error\":\"outbound_drain_failed\""));
        EXPECT_TRUE(ContainsPayload(outputState, "\"event\":\"outbound_publication_failed\""));
        EXPECT_TRUE(ContainsPayload(outputState, "ticks=7 ingressEvents=5"));
        EXPECT_TRUE(ContainsPayload(outputState, "maxTickStartLagNs=13 maxTickDurationNs=17"));
        EXPECT_TRUE(ContainsPayload(outputState, "\"event\":\"host_failed\""));
        EXPECT_TRUE(ContainsPayload(outputState, "\"event\":\"logging_health_summary\""));
    }
} // namespace psnr::world::tests
