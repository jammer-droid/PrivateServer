#include "WorldServerHostRunner.h"

#include "NrServerWorldEventSource.h"
#include "WorldApplicationLogAdapter.h"
#include "WorldDoubleBufferedWorkers.h"
#include "WorldEntityManager.h"
#include "WorldExecutionModeConfig.h"
#include "WorldExecutionStorage.h"
#include "WorldIngressEventConsumer.h"
#include "WorldMovementCommandStore.h"
#include "WorldPacketTypes.h"
#include "WorldServerHostLog.h"
#include "WorldServerHostChildControlWorker.h"
#include "WorldServerHostArtifactWriteQueue.h"
#include "WorldServerHostArtifactWriter.h"
#include "WorldServerHostStopSignal.h"
#include "WorldSessionRegistry.h"
#include "WorldTickSampleBuffer.h"
#include "WorldWorkerShutdown.h"
#include "WorldWorkerStartup.h"

#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerConfig.h>
#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace psnr::world::host
{
    namespace
    {
        constexpr std::uint32_t FirstServerTick = 1;
        constexpr std::string_view TickSampleArtifactFileName = "tick-samples.jsonl";
        constexpr std::string_view RuntimeSampleArtifactFileName = "samples.jsonl";
        constexpr std::string_view RuntimeReportFileName = "report.json";
        constexpr WorldExecutionModeConfig DoubleBufferedExecutionModes{
            WorldInboundMode::DoubleBuffered,
            WorldOutboundMode::DoubleBuffered,
        };
        constexpr std::array<psnr::core::NrPacketType, protocol::C2SWorldIngressPacketTypes.size()>
            WorldIngressPacketTypes = {
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::JoinWorldRequest)},
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput)},
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::WorldTimeSyncRequest)},
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::ControlStateCommand)},
        };
    } // namespace

    class WorldServerHostRunner::RuntimeShutdownAdapter final
    {
    public:
        explicit RuntimeShutdownAdapter(psnr::runtime::NrServer& server) noexcept
            : server_(server)
        {
        }

        [[nodiscard]] bool RequestStopAndShutdown() noexcept
        {
            requestStopStatus_ = server_.RequestStop();
            shutdownStatus_ = server_.Shutdown();
            return requestStopStatus_.Succeeded() && shutdownStatus_.Succeeded();
        }

        [[nodiscard]] psnr::core::NrStatus RequestStopStatus() const noexcept
        {
            return requestStopStatus_;
        }

        [[nodiscard]] psnr::core::NrStatus ShutdownStatus() const noexcept
        {
            return shutdownStatus_;
        }

    private:
        psnr::runtime::NrServer& server_;
        psnr::core::NrStatus requestStopStatus_{};
        psnr::core::NrStatus shutdownStatus_{};
    };

    int WorldServerHostRunner::ReportRuntimeFailure(const WorldServerHostLog& log, const std::string_view operation,
                                                    const psnr::core::NrStatus status)
    {
        log.RuntimeFailure(operation, status);
        return 1;
    }

    void WorldServerHostRunner::StopRuntimeAfterStartupFailure(const WorldServerHostLog& log,
                                                               psnr::runtime::NrServer& server)
    {
        const psnr::core::NrStatus requestStopStatus = server.RequestStop();
        if (requestStopStatus.Failed())
        {
            log.RuntimeFailure("nr_server_request_stop", requestStopStatus);
        }
        else
        {
            log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "runtime_request_stop_completed",
                          "nr_server_request_stop", "completed");
        }

        const psnr::core::NrStatus shutdownStatus = server.Shutdown();
        if (shutdownStatus.Failed())
        {
            log.RuntimeFailure("nr_server_shutdown", shutdownStatus);
        }
        else
        {
            log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "runtime_shutdown_completed",
                          "nr_server_shutdown", "completed");
        }
    }

    bool WorldServerHostRunner::CaptureAndSubmitRuntimeSample(psnr::runtime::NrServer& server,
                                                              WorldServerHostArtifactWriteQueue& artifactWriteQueue,
                                                              const WorldServerHostLog& log,
                                                              const std::uint64_t sequence,
                                                              const std::uint64_t elapsedMilliseconds,
                                                              const std::string_view captureOperation)
    {
        const std::chrono::steady_clock::time_point captureStarted = std::chrono::steady_clock::now();
        psnr::runtime::NrServerSnapshot runtimeSnapshot;
        const psnr::core::NrStatus captureStatus = server.CaptureSnapshot(&runtimeSnapshot);
        const std::chrono::nanoseconds captureDuration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - captureStarted);
        if (captureStatus.Failed())
        {
            log.RuntimeFailure(captureOperation, captureStatus);
        }

        WorldServerHostRuntimeSample runtimeSample;
        runtimeSample.sequence = sequence;
        runtimeSample.elapsedMilliseconds = elapsedMilliseconds;
        runtimeSample.captureDurationNanoseconds = static_cast<std::uint64_t>(captureDuration.count());
        runtimeSample.captureStatus = captureStatus;
        runtimeSample.snapshot = std::move(runtimeSnapshot);
        const WorldServerHostArtifactWriteQueueResult submitResult =
            artifactWriteQueue.TryPushRuntimeSample(std::move(runtimeSample));
        if (submitResult != WorldServerHostArtifactWriteQueueResult::Succeeded)
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "runtime_sample_submit_failed",
                           "host_artifact_write_queue", "failed");
        }
        return captureStatus.Succeeded() && submitResult == WorldServerHostArtifactWriteQueueResult::Succeeded;
    }

    int WorldServerHostRunner::Run(const WorldServerHostConfig& config,
                                   const std::filesystem::path& runtimeDiagnosticsPath,
                                   const std::filesystem::path& worldArtifactDirectory, const std::string_view runId,
                                   const WorldServerHostLog& log,
                                   const psnr::logging::ApplicationLogHandle worldApplicationLog,
                                   WorldServerHostChildControl* const childControl,
                                   WorldServerHostStopSignal& stopSignal)
    {
        const std::string diagnosticsPath = runtimeDiagnosticsPath.generic_string();
        psnr::runtime::NrServerConfig runtimeConfig;
        runtimeConfig.bindEndpoint.ipv4Address = psnr::runtime::NrIPv4Address{
            config.network.bindAddress[0],
            config.network.bindAddress[1],
            config.network.bindAddress[2],
            config.network.bindAddress[3],
        };
        runtimeConfig.bindEndpoint.port = config.network.port;
        runtimeConfig.listenBacklog = config.network.listenBacklog;
        runtimeConfig.acceptSlotCount = config.network.acceptSlotCount;
        runtimeConfig.actorMailboxCapacity = config.network.actorMailboxCapacity;
        runtimeConfig.pendingSendQueueCapacity = config.network.pendingSendQueueCapacity;
        runtimeConfig.maxSessionCount = config.network.maxSessionCount;
        runtimeConfig.toWorldEventCapacity = config.network.toWorldEventCapacity;
        runtimeConfig.payloadPools.payload64BlockCount = config.network.payloadPools.payload64BlockCount;
        runtimeConfig.payloadPools.payload256BlockCount = config.network.payloadPools.payload256BlockCount;
        runtimeConfig.payloadPools.payload1024BlockCount = config.network.payloadPools.payload1024BlockCount;
        runtimeConfig.payloadPools.payload8192BlockCount = config.network.payloadPools.payload8192BlockCount;
        runtimeConfig.payloadPools.payloadRefControlBlockCount =
            config.network.payloadPools.payloadRefControlBlockCount;
        runtimeConfig.diagnostics.mode = psnr::runtime::NrDiagnosticsMode::Benchmark;
        runtimeConfig.diagnostics.outputPath = psnr::runtime::NrUtf8View{
            diagnosticsPath.data(),
            static_cast<std::uint32_t>(diagnosticsPath.size()),
        };
        runtimeConfig.additionalWorldIngressPacketTypes = psnr::runtime::NrPacketTypeView{
            WorldIngressPacketTypes.data(),
            static_cast<std::uint32_t>(WorldIngressPacketTypes.size()),
        };

        psnr::runtime::NrServer server;
        psnr::core::NrStatus status = psnr::runtime::NrServer::Create(runtimeConfig, &server);
        if (status.Failed())
        {
            return ReportRuntimeFailure(log, "nr_server_create", status);
        }
        log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "runtime_create_completed", "nr_server_create",
                      "completed");

        status = server.Start();
        if (status.Failed())
        {
            return ReportRuntimeFailure(log, "nr_server_start", status);
        }
        log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "runtime_start_completed", "nr_server_start",
                      "completed");

        psnr::runtime::NrGateway gateway;
        status = server.CreateGateway(&gateway);
        if (status.Failed())
        {
            const int result = ReportRuntimeFailure(log, "nr_server_create_gateway", status);
            StopRuntimeAfterStartupFailure(log, server);
            return result;
        }
        log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "gateway_create_completed",
                      "nr_server_create_gateway", "completed");

        WorldResult<std::unique_ptr<WorldExecutionStorage>> storageResult =
            WorldExecutionStorage::Create(WorldExecutionStorageConfig{
                DoubleBufferedExecutionModes,
                config.execution.inboundEventCapacityPerSlot,
                config.execution.outboundCapacityPerSlot,
                WorldOutboundBufferSlotCount::Triple,
            });
        if (storageResult.Failed())
        {
            log.StorageCreateFailure(storageResult.Error());
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }
        std::unique_ptr<WorldExecutionStorage> executionStorage = storageResult.TakeValue();

        log.NetworkListening(config.channel.id, config.channel.name, config.network.bindAddress, config.network.port);
        return RunWorld(server, gateway, *executionStorage, log, worldApplicationLog, config, worldArtifactDirectory,
                        runtimeDiagnosticsPath.parent_path() / RuntimeReportFileName, runId, childControl, stopSignal);
    }

    int WorldServerHostRunner::RunWorld(psnr::runtime::NrServer& server, psnr::runtime::NrGateway& gateway,
                                        WorldExecutionStorage& storage, const WorldServerHostLog& log,
                                        const psnr::logging::ApplicationLogHandle worldApplicationLog,
                                        const WorldServerHostConfig& config,
                                        const std::filesystem::path& worldArtifactDirectory,
                                        const std::filesystem::path& runtimeReportPath, const std::string_view runId,
                                        WorldServerHostChildControl* const childControl,
                                        WorldServerHostStopSignal& stopSignal)
    {
        WorldIngressDoubleBuffer* const ingressBuffer = storage.InboundBuffer();
        WorldOutboundDoubleBuffer* const outboundBuffer = storage.OutboundBuffer();
        if (ingressBuffer == nullptr || outboundBuffer == nullptr)
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "storage_selection_failed",
                           "world_storage_selection", "failed", "incomplete_storage");
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementCommandStore;
        NrServerWorldEventSource eventSource{server};
        WorldApplicationLogAdapter applicationEvents{worldApplicationLog};
        WorldIngressEventConsumer eventConsumer{
            sessionRegistry,
            entityManager,
            movementCommandStore,
            server,
            gateway,
            WorldOutboundMode::DoubleBuffered,
            outboundBuffer,
            config.consumer,
            FirstServerTick,
            FirstServerTick - 1,
            applicationEvents,
        };

        const std::chrono::nanoseconds fixedStep{1000000000ull / config.execution.tickRateHz};
        const WorldClock::time_point firstDeadline = WorldClock::now() + fixedStep;
        const WorldDoubleBufferedTickConfig tickConfig{
            fixedStep,
            config.execution.maxCatchUpSteps,
            1,
            FirstServerTick,
            FirstServerTick - 1,
            firstDeadline,
            WorldOutboundMode::DoubleBuffered,
            config.tickProcessor,
        };

        WorldIngressPump pump{*ingressBuffer};
        WorldOutboundPublisher publisher{*outboundBuffer};
        WorldIngressWorkerExchange ingressExchange;
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> tickSampleBufferResult =
            WorldTickSampleBuffer::Create(config.execution.tickSampleCapacity);
        if (tickSampleBufferResult.Failed())
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "tick_sample_buffer_create_failed",
                           "world_tick_sample_buffer", "failed", "invalid_capacity_or_allocation_failure");
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }
        std::unique_ptr<WorldTickSampleBuffer> tickSampleBuffer = tickSampleBufferResult.TakeValue();
        WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> artifactWriteQueueResult =
            WorldServerHostArtifactWriteQueue::Create(config.execution.tickSampleWriteQueueCapacity,
                                                      config.execution.runtimeSampleWriteQueueCapacity);
        if (artifactWriteQueueResult.Failed())
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "artifact_write_queue_create_failed",
                           "host_artifact_write_queue", "failed", "invalid_capacity_or_allocation_failure");
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }
        std::unique_ptr<WorldServerHostArtifactWriteQueue> artifactWriteQueue = artifactWriteQueueResult.TakeValue();
        WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus>
            artifactWriterResult =
                WorldServerHostArtifactWriter::Create(*artifactWriteQueue, config.channel.id, config.channel.name,
                                                      runId, worldArtifactDirectory / TickSampleArtifactFileName,
                                                      runtimeReportPath.parent_path() / RuntimeSampleArtifactFileName,
                                                      runtimeReportPath);
        if (artifactWriterResult.Failed())
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "artifact_writer_create_failed",
                           "host_artifact_writer", "failed");
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }
        std::unique_ptr<WorldServerHostArtifactWriter> artifactWriter = artifactWriterResult.TakeValue();
        if (artifactWriter->Start() != WorldServerHostArtifactWriterStatus::Running)
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "artifact_writer_start_failed",
                           "host_artifact_writer", "failed");
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }
        WorldDoubleBufferedTickCoordinator coordinator{
            tickConfig,    *ingressBuffer, sessionRegistry,        movementCommandStore,
            entityManager, outboundBuffer, tickSampleBuffer.get(),
        };
        WorldOutboundPublisherWorker publisherWorker{publisher, gateway};
        WorldIngressPumpWorker pumpWorker{pump, eventSource, ingressExchange};
        WorldDoubleBufferedCoordinatorWorker coordinatorWorker{
            WorldDoubleBufferedCoordinatorWorkerConfig{
                std::chrono::milliseconds{config.execution.shutdownDrainTimeoutMilliseconds},
            },
            coordinator,
            *ingressBuffer,
            eventConsumer,
            ingressExchange,
            std::move(tickSampleBuffer),
            artifactWriteQueue.get(),
        };

        const WorldWorkerStartupReport startupReport =
            WorldWorkerStartup::Start(DoubleBufferedExecutionModes, &publisherWorker, &pumpWorker, coordinatorWorker);
        if (startupReport.result != WorldWorkerStartupResult::Started)
        {
            artifactWriter->CloseAndJoin();
            log.WorkerStartupFailure(startupReport.failedWorker);
            StopRuntimeAfterStartupFailure(log, server);
            return 1;
        }

        const std::chrono::steady_clock::time_point runtimeSamplingStarted = std::chrono::steady_clock::now();
        const std::chrono::milliseconds runtimeSampleInterval{
            config.execution.runtimeSampleIntervalMilliseconds,
        };
        std::chrono::steady_clock::time_point nextRuntimeSampleDeadline =
            runtimeSamplingStarted + runtimeSampleInterval;
        std::uint64_t runtimeSampleSequence = 0;
        std::uint64_t runtimeSampleCollectionFailureCount = 0;

        log.WorldReady();
        std::unique_ptr<WorldServerHostChildControlWorker> childControlWorker;
        if (childControl != nullptr)
        {
            childControlWorker = std::make_unique<WorldServerHostChildControlWorker>(*childControl, stopSignal);
            if (!childControlWorker->Start())
            {
                log.HostEvent(psnr::logging::ApplicationLogSeverity::Error, "child_control_worker_start_failed",
                              "child_control_worker_start", "failed");
                stopSignal.Request();
            }
        }

        while (!stopSignal.IsRequested())
        {
            const std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
            if (currentTime >= nextRuntimeSampleDeadline)
            {
                ++runtimeSampleSequence;
                const std::chrono::milliseconds elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - runtimeSamplingStarted);
                if (!CaptureAndSubmitRuntimeSample(server, *artifactWriteQueue, log, runtimeSampleSequence,
                                                   static_cast<std::uint64_t>(elapsed.count()),
                                                   "nr_server_capture_periodic_snapshot"))
                {
                    ++runtimeSampleCollectionFailureCount;
                }

                // Host가 지연된 경우 과거 cadence를 연속 실행하지 않고 현재 시점부터 다음 주기를 계산한다.
                nextRuntimeSampleDeadline = currentTime + runtimeSampleInterval;
            }

            WorldWorkerKind stoppedWorker = WorldWorkerKind::None;
            const WorldConcreteWorkerStopReason publisherStopReason = publisherWorker.StopReason();
            const WorldConcreteWorkerStopReason ingressStopReason = pumpWorker.StopReason();
            const WorldConcreteWorkerStopReason coordinatorStopReason = coordinatorWorker.StopReason();
            WorldConcreteWorkerStopReason stopReason = publisherStopReason;
            if (stopReason != WorldConcreteWorkerStopReason::Running)
            {
                stoppedWorker = WorldWorkerKind::OutboundPublisher;
            }
            else
            {
                stopReason = ingressStopReason;
                if (stopReason != WorldConcreteWorkerStopReason::Running)
                {
                    stoppedWorker = WorldWorkerKind::IngressPump;
                }
                else
                {
                    stopReason = coordinatorStopReason;
                    if (stopReason != WorldConcreteWorkerStopReason::Running)
                    {
                        stoppedWorker = WorldWorkerKind::Coordinator;
                    }
                }
            }

            if (stoppedWorker != WorldWorkerKind::None)
            {
                log.WorkerStoppedUnexpectedly(stoppedWorker, stopReason);
                log.RecordUnexpectedWorkerStopDetail(publisherStopReason, ingressStopReason, coordinatorStopReason,
                                                     coordinatorWorker.LastTickReport(),
                                                     outboundBuffer->WritableUsage(), outboundBuffer->CapacityPerSlot(),
                                                     outboundBuffer->LastAppendFailure());
                stopSignal.Request();
                break;
            }
            Sleep(10);
        }

        log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "stop_request_observed", "host_control", "observed");

        RuntimeShutdownAdapter runtime{server};
        const WorldWorkerShutdownReport shutdownReport = WorldWorkerShutdown::Run(
            DoubleBufferedExecutionModes, runtime, &publisherWorker, &pumpWorker, coordinatorWorker);
        const std::chrono::steady_clock::time_point terminalSampleTime = std::chrono::steady_clock::now();
        const std::chrono::milliseconds terminalElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(terminalSampleTime - runtimeSamplingStarted);
        ++runtimeSampleSequence;
        const bool terminalRuntimeSampleCompleted = CaptureAndSubmitRuntimeSample(
            server, *artifactWriteQueue, log, runtimeSampleSequence,
            static_cast<std::uint64_t>(terminalElapsed.count()), "nr_server_capture_terminal_snapshot");
        if (!terminalRuntimeSampleCompleted)
        {
            ++runtimeSampleCollectionFailureCount;
        }

        artifactWriter->CloseAndJoin(coordinatorWorker.TickSampleCollectionFailureCount(),
                                     shutdownReport.result == WorldWorkerShutdownResult::Completed);
        const bool artifactWriterCompleted = artifactWriter->Status() == WorldServerHostArtifactWriterStatus::Completed;
        if (!artifactWriterCompleted)
        {
            log.WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "artifact_writer_failed",
                           "host_artifact_writer", "failed");
        }
        const bool runtimeArtifactCompleted =
            terminalRuntimeSampleCompleted && runtimeSampleCollectionFailureCount == 0 && artifactWriterCompleted;

        const WorldIngressPumpMetrics ingressMetrics = pump.Metrics();
        const WorldDoubleBufferedTickMetrics tickMetrics = coordinator.Metrics();
        const WorldOutboundPublisherMetrics outboundMetrics = publisher.Metrics();
        log.RecordWorkerShutdown(shutdownReport, ingressMetrics, tickMetrics, outboundMetrics);

        bool childControlCompleted = true;
        if (childControlWorker != nullptr)
        {
            if (childControlWorker->Result() == WorldServerHostChildControlWorkerResult::StopReceived)
            {
                childControlWorker->NotifyShutdownCompleted(shutdownReport.result ==
                                                                WorldWorkerShutdownResult::Completed &&
                                                            artifactWriterCompleted && runtimeArtifactCompleted);
            }
            else
            {
                childControlWorker->RequestStop();
            }
            childControlWorker->Join();
            childControlCompleted = childControlWorker->Result() == WorldServerHostChildControlWorkerResult::Completed;
            if (!childControlCompleted)
            {
                log.HostEvent(psnr::logging::ApplicationLogSeverity::Error, "child_control_worker_failed",
                              "child_control_worker", "failed");
            }
        }

        if (runtime.RequestStopStatus().Failed())
        {
            log.RuntimeFailure("nr_server_request_stop", runtime.RequestStopStatus());
        }
        else
        {
            log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "runtime_request_stop_completed",
                          "nr_server_request_stop", "completed");
        }
        if (runtime.ShutdownStatus().Failed())
        {
            log.RuntimeFailure("nr_server_shutdown", runtime.ShutdownStatus());
        }
        else
        {
            log.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "runtime_shutdown_completed",
                          "nr_server_shutdown", "completed");
        }
        if (shutdownReport.result != WorldWorkerShutdownResult::Completed || !artifactWriterCompleted ||
            !runtimeArtifactCompleted || !childControlCompleted)
        {
            return 1;
        }
        return 0;
    }
} // namespace psnr::world::host
