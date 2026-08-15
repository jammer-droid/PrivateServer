#include "BenchmarkServerChildRunner.h"

#include "BenchmarkEndpointParser.h"
#include "BenchmarkIpcCommand.h"
#include "BenchmarkIpcEvent.h"
#include "BenchmarkIpcPipe.h"
#include "BenchmarkProtocol.h"
#include "BenchmarkRuntimeSampleProjection.h"
#include "BenchmarkServerEchoWorker.h"

#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr int ChildSuccessExitCode = 0;
        constexpr int ChildFailureExitCode = 1;

        [[nodiscard]] std::string StatusError(const std::string_view operation, const psnr::core::NrStatus status)
        {
            std::string error(operation);
            error.append(" failed with errorCode=");
            error.append(std::to_string(static_cast<int>(status.ErrorCode())));
            error.append(" nativeErrorCode=");
            error.append(std::to_string(status.NativeErrorCode()));
            return error;
        }

        [[nodiscard]] bool TryBuildDiagnosticsPath(const std::string_view runId,
                                                   const BenchmarkArtifactConfigV1& artifactConfig,
                                                   std::string* const outPath)
        {
            if (runId.empty() || artifactConfig.outputRoot.empty() || outPath == nullptr)
            {
                return false;
            }

            try
            {
                const std::u8string encodedOutputRoot(artifactConfig.outputRoot.cbegin(),
                                                      artifactConfig.outputRoot.cend());
                const std::u8string encodedRunId(runId.cbegin(), runId.cend());
                const std::filesystem::path path =
                    std::filesystem::path(encodedOutputRoot) / encodedRunId / "diagnostics.jsonl";
                const std::u8string encodedPath = path.u8string();
                if (encodedPath.empty() || encodedPath.size() > std::numeric_limits<std::uint32_t>::max())
                {
                    return false;
                }

                outPath->assign(encodedPath.cbegin(), encodedPath.cend());
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool WriteEvent(const BenchmarkIpcEventV1& event, BenchmarkIpcLineWriter* const writer,
                                      std::string* const outError)
        {
            const BenchmarkIpcEventEncodeResult encodeResult = BenchmarkIpcEventV1Codec::Encode(event);
            if (!encodeResult.Succeeded())
            {
                if (outError != nullptr)
                {
                    *outError = encodeResult.error;
                }
                return false;
            }

            const BenchmarkIpcIoResult writeResult = writer->WriteLine(encodeResult.json);
            if (!writeResult.Succeeded())
            {
                if (outError != nullptr)
                {
                    *outError = writeResult.error;
                }
                return false;
            }
            return true;
        }

        void WriteErrorBestEffort(const std::string_view runId, const std::uint64_t sequence,
                                  const std::string_view message, BenchmarkIpcLineWriter* const writer) noexcept
        {
            try
            {
                BenchmarkIpcEventV1 event;
                event.type = BenchmarkIpcEventType::Error;
                event.runId = runId;
                event.sequence = sequence;
                event.errorMessage = message;
                std::string ignoredError;
                static_cast<void>(WriteEvent(event, writer, &ignoredError));
            }
            catch (...)
            {
            }
        }

        void StopAndShutdownBestEffort(psnr::runtime::NrServer* const server,
                                       BenchmarkServerEchoWorker* const echoWorker) noexcept
        {
            if (echoWorker != nullptr)
            {
                echoWorker->BeginDrainOnly();
            }

            if (server != nullptr && server->IsValid())
            {
                static_cast<void>(server->RequestStop());
                static_cast<void>(server->Shutdown());
            }

            if (echoWorker != nullptr)
            {
                echoWorker->DrainRemainingAndJoin();
            }
        }

        [[nodiscard]] int FailRunningServer(psnr::runtime::NrServer* const server,
                                            BenchmarkServerEchoWorker* const echoWorker,
                                            BenchmarkIpcLineWriter* const writer, const std::string_view runId,
                                            const std::uint64_t sequence, const std::string_view error) noexcept
        {
            WriteErrorBestEffort(runId, sequence, error, writer);
            StopAndShutdownBestEffort(server, echoWorker);
            return ChildFailureExitCode;
        }
    } // namespace

    int BenchmarkServerChildRunner::Run(const std::string_view runId, const BenchmarkServerConfigV1& serverConfig,
                                        const BenchmarkArtifactConfigV1& artifactConfig,
                                        const std::uint64_t commandPipeHandleValue,
                                        const std::uint64_t eventPipeHandleValue)
    {
        if (commandPipeHandleValue == 0 || eventPipeHandleValue == 0 ||
            commandPipeHandleValue > std::numeric_limits<std::uintptr_t>::max() ||
            eventPipeHandleValue > std::numeric_limits<std::uintptr_t>::max() ||
            commandPipeHandleValue == eventPipeHandleValue)
        {
            return ChildFailureExitCode;
        }

        BenchmarkIpcOwnedHandle commandReadHandle(
            reinterpret_cast<BenchmarkIpcPipeHandle>(static_cast<std::uintptr_t>(commandPipeHandleValue)));
        BenchmarkIpcOwnedHandle eventWriteHandle(
            reinterpret_cast<BenchmarkIpcPipeHandle>(static_cast<std::uintptr_t>(eventPipeHandleValue)));

        BenchmarkIpcLineReader commandReader(std::move(commandReadHandle));
        BenchmarkIpcLineWriter eventWriter(std::move(eventWriteHandle));
        psnr::runtime::NrServer server;
        BenchmarkServerEchoWorker echoWorker;

        try
        {
            psnr::runtime::NrEndpoint endpoint;
            if (!BenchmarkEndpointParser::TryParseServerEndpoint(serverConfig, &endpoint))
            {
                WriteErrorBestEffort(runId, 0, "server.address must be a dotted-decimal IPv4 address", &eventWriter);
                return ChildFailureExitCode;
            }

            psnr::runtime::NrServerConfig runtimeConfig;
            runtimeConfig.bindEndpoint = endpoint;
            runtimeConfig.toWorldEventCapacity = psnr::runtime::NrDefaultToWorldEventCapacity * 2;
            std::string diagnosticsPath;
            if (!TryBuildDiagnosticsPath(runId, artifactConfig, &diagnosticsPath))
            {
                WriteErrorBestEffort(runId, 0, "failed to build diagnostics artifact path", &eventWriter);
                return ChildFailureExitCode;
            }
            runtimeConfig.diagnostics.mode = psnr::runtime::NrDiagnosticsMode::Benchmark;
            runtimeConfig.diagnostics.outputPath = psnr::runtime::NrUtf8View{
                diagnosticsPath.data(),
                static_cast<std::uint32_t>(diagnosticsPath.size()),
            };
            const std::array<psnr::runtime::NrPacketType, 1> worldIngressPacketTypes = {
                psnr::runtime::NrPacketType{BenchmarkRequestPacketType},
            };
            runtimeConfig.additionalWorldIngressPacketTypes = psnr::runtime::NrPacketTypeView{
                worldIngressPacketTypes.data(),
                static_cast<std::uint32_t>(worldIngressPacketTypes.size()),
            };

            const psnr::core::NrStatus createStatus = psnr::runtime::NrServer::Create(runtimeConfig, &server);
            if (createStatus.Failed())
            {
                WriteErrorBestEffort(runId, 0, StatusError("NrServer::Create", createStatus), &eventWriter);
                return ChildFailureExitCode;
            }

            const psnr::core::NrStatus startStatus = server.Start();
            if (startStatus.Failed())
            {
                WriteErrorBestEffort(runId, 0, StatusError("NrServer::Start", startStatus), &eventWriter);
                static_cast<void>(server.Shutdown());
                return ChildFailureExitCode;
            }

            std::string echoWorkerError;
            if (!echoWorker.Start(&server, &echoWorkerError))
            {
                WriteErrorBestEffort(runId, 0, echoWorkerError, &eventWriter);
                StopAndShutdownBestEffort(&server, &echoWorker);
                return ChildFailureExitCode;
            }

            BenchmarkIpcEventV1 readyEvent;
            readyEvent.type = BenchmarkIpcEventType::Ready;
            readyEvent.runId = runId;
            readyEvent.sequence = 0;
            std::string eventError;
            if (!WriteEvent(readyEvent, &eventWriter, &eventError))
            {
                StopAndShutdownBestEffort(&server, &echoWorker);
                return ChildFailureExitCode;
            }

            while (true)
            {
                std::string commandLine;
                const BenchmarkIpcReadResult readResult = commandReader.ReadLine(&commandLine);
                if (!readResult.Succeeded())
                {
                    return FailRunningServer(&server, &echoWorker, &eventWriter, runId, 0, readResult.error);
                }
                if (readResult.outcome == BenchmarkIpcReadOutcome::EndOfStream)
                {
                    StopAndShutdownBestEffort(&server, &echoWorker);
                    return ChildFailureExitCode;
                }

                const BenchmarkIpcCommandDecodeResult decodeResult = BenchmarkIpcCommandV1Codec::Decode(commandLine);
                if (!decodeResult.Succeeded())
                {
                    return FailRunningServer(&server, &echoWorker, &eventWriter, runId, 0, decodeResult.error);
                }

                const BenchmarkIpcCommandV1& command = decodeResult.command;
                if (command.runId != runId)
                {
                    return FailRunningServer(&server, &echoWorker, &eventWriter, runId, command.sequence,
                                             "command runId does not match child runId");
                }

                if (echoWorker.TryGetFailure(&echoWorkerError))
                {
                    return FailRunningServer(&server, &echoWorker, &eventWriter, runId, command.sequence,
                                             echoWorkerError);
                }

                if (command.type == BenchmarkIpcCommandType::CaptureSnapshot)
                {
                    psnr::runtime::NrServerSnapshot snapshot;
                    const psnr::core::NrStatus snapshotStatus = server.CaptureSnapshot(&snapshot);
                    if (snapshotStatus.Failed())
                    {
                        return FailRunningServer(&server, &echoWorker, &eventWriter, runId, command.sequence,
                                                 StatusError("NrServer::CaptureSnapshot", snapshotStatus));
                    }

                    const BenchmarkRuntimeSampleProjectionResult projectionResult =
                        BenchmarkRuntimeSampleProjection::Project(snapshot);
                    if (!projectionResult.Succeeded())
                    {
                        return FailRunningServer(&server, &echoWorker, &eventWriter, runId, command.sequence,
                                                 projectionResult.error);
                    }

                    BenchmarkIpcEventV1 sampleEvent;
                    sampleEvent.type = BenchmarkIpcEventType::RuntimeSample;
                    sampleEvent.runId = runId;
                    sampleEvent.sequence = command.sequence;
                    sampleEvent.runtimeSample = projectionResult.sample;
                    if (!WriteEvent(sampleEvent, &eventWriter, &eventError))
                    {
                        StopAndShutdownBestEffort(&server, &echoWorker);
                        return ChildFailureExitCode;
                    }
                    continue;
                }

                if (command.type == BenchmarkIpcCommandType::Stop)
                {
                    echoWorker.BeginDrainOnly();
                    const psnr::core::NrStatus requestStopStatus = server.RequestStop();
                    const psnr::core::NrStatus shutdownStatus = server.Shutdown();
                    echoWorker.DrainRemainingAndJoin();
                    if (requestStopStatus.Failed())
                    {
                        WriteErrorBestEffort(runId, command.sequence,
                                             StatusError("NrServer::RequestStop", requestStopStatus), &eventWriter);
                        return ChildFailureExitCode;
                    }
                    if (shutdownStatus.Failed())
                    {
                        WriteErrorBestEffort(runId, command.sequence, StatusError("NrServer::Shutdown", shutdownStatus),
                                             &eventWriter);
                        return ChildFailureExitCode;
                    }
                    if (echoWorker.TryGetFailure(&echoWorkerError))
                    {
                        WriteErrorBestEffort(runId, command.sequence, echoWorkerError, &eventWriter);
                        return ChildFailureExitCode;
                    }

                    BenchmarkIpcEventV1 stoppedEvent;
                    stoppedEvent.type = BenchmarkIpcEventType::Stopped;
                    stoppedEvent.runId = runId;
                    stoppedEvent.sequence = command.sequence;
                    if (!WriteEvent(stoppedEvent, &eventWriter, &eventError))
                    {
                        return ChildFailureExitCode;
                    }
                    return ChildSuccessExitCode;
                }

                return FailRunningServer(&server, &echoWorker, &eventWriter, runId, command.sequence,
                                         "command type is unsupported");
            }
        }
        catch (...)
        {
            return FailRunningServer(&server, &echoWorker, &eventWriter, runId, 0,
                                     "Server child failed with an exception");
        }
    }
} // namespace psnr::benchmark
