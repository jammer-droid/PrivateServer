#include "BenchmarkWorldHostController.h"

#include "ApplicationBuildInfo.h"
#include "BenchmarkIpcCommand.h"
#include "BenchmarkIpcEvent.h"
#include "BenchmarkEndpointParser.h"
#include "BenchmarkRunArtifact.h"
#include "BenchmarkServerChildProcess.h"
#include "BenchmarkServerProcessSampler.h"
#include "BenchmarkWorldArtifactSummary.h"
#include "BenchmarkWorldClientWorkload.h"
#include "BenchmarkWorldMergedArtifact.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        constexpr int ControllerSuccessExitCode = 0;
        constexpr int ControllerFailureExitCode = 1;
        constexpr std::uint64_t StopCommandSequence = 1;
        constexpr std::string_view EffectiveConfigSchema = "psnr.benchmark.world_host.effective_config";
        constexpr std::uint64_t EffectiveConfigVersion = 1;
        constexpr std::string_view ClientResultSchema = "psnr.benchmark.world_host.client_result";
        constexpr std::uint64_t ClientResultVersion = 1;
        constexpr std::string_view ProcessSampleSchema = "psnr.benchmark.world_host.process_sample";
        constexpr std::uint64_t ProcessSampleVersion = 1;

        using JsonObject = nlohmann::ordered_json;

        class BenchmarkWorldHostProcessSamplingWorker final
        {
        public:
            ~BenchmarkWorldHostProcessSamplingWorker() noexcept
            {
                static_cast<void>(Stop());
            }

            BenchmarkWorldHostProcessSamplingWorker() = default;

            BenchmarkWorldHostProcessSamplingWorker(const BenchmarkWorldHostProcessSamplingWorker&) = delete;
            BenchmarkWorldHostProcessSamplingWorker& operator=(const BenchmarkWorldHostProcessSamplingWorker&) = delete;

            [[nodiscard]] std::string Start(const BenchmarkNativeProcessHandle processHandle,
                                            const std::filesystem::path& runsRoot, const std::string& runId,
                                            const std::uint32_t intervalMilliseconds)
            {
                if (worker_.joinable() || processHandle == nullptr || runId.empty() || intervalMilliseconds == 0)
                {
                    return "World Host process sampling worker start state is invalid";
                }

                std::error_code filesystemError;
                const std::filesystem::path runDirectory = runsRoot / runId;
                if (!std::filesystem::is_directory(runDirectory, filesystemError) || filesystemError)
                {
                    return "World Host run directory is unavailable for process sampling";
                }

                const std::filesystem::path benchmarkDirectory = runDirectory / "benchmark";
                std::filesystem::create_directory(benchmarkDirectory, filesystemError);
                if (filesystemError)
                {
                    return "failed to create World Host benchmark artifact directory";
                }

                output_.open(benchmarkDirectory / "process-samples.jsonl", std::ios::binary | std::ios::trunc);
                if (!output_.is_open())
                {
                    return "failed to open World Host process sample artifact";
                }

                processHandle_ = processHandle;
                runId_ = runId;
                interval_ = std::chrono::milliseconds{intervalMilliseconds};
                samples_.clear();
                phase_.store(BenchmarkWorldMeasurementPhase::Admission, std::memory_order_release);
                stopRequested_.store(false, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lock{mutex_};
                    error_.clear();
                }

                try
                {
                    worker_ = std::thread{&BenchmarkWorldHostProcessSamplingWorker::Run, this};
                }
                catch (const std::system_error& exception)
                {
                    output_.close();
                    return std::string{"failed to start World Host process sampling worker: "} + exception.what();
                }
                return {};
            }

            void BeginMeasurement() noexcept
            {
                phase_.store(BenchmarkWorldMeasurementPhase::Measurement, std::memory_order_release);
            }

            void EndMeasurement() noexcept
            {
                phase_.store(BenchmarkWorldMeasurementPhase::Drain, std::memory_order_release);
            }

            [[nodiscard]] const std::vector<BenchmarkWorldProcessSampleV1>& Samples() const noexcept
            {
                return samples_;
            }

            [[nodiscard]] std::string Stop() noexcept
            {
                stopRequested_.store(true, std::memory_order_release);
                wake_.notify_one();
                if (worker_.joinable())
                {
                    worker_.join();
                }
                if (output_.is_open())
                {
                    output_.flush();
                    output_.close();
                }

                // worker의 SetError()와 동일한 mutex로 error_ 접근 규칙을 유지하기 위해 방어적으로 보호한다.
                std::lock_guard<std::mutex> lock{mutex_};
                return error_;
            }

        private:
            void Run() noexcept
            {
                try
                {
                    BenchmarkServerProcessSampler sampler{processHandle_};
                    std::uint64_t sequence = 1;
                    std::chrono::steady_clock::time_point nextSampleAt = std::chrono::steady_clock::now();
                    while (!stopRequested_.load(std::memory_order_acquire))
                    {
                        const BenchmarkServerProcessSampleResult sampleResult = sampler.Capture(sequence);
                        if (!sampleResult.Succeeded())
                        {
                            SetError(sampleResult.error);
                            return;
                        }
                        if (!AppendSample(sampleResult.sample))
                        {
                            return;
                        }
                        ++sequence;

                        nextSampleAt += interval_;
                        const std::chrono::steady_clock::time_point sampleCompletedAt =
                            std::chrono::steady_clock::now();
                        while (nextSampleAt <= sampleCompletedAt)
                        {
                            nextSampleAt += interval_;
                        }

                        std::unique_lock<std::mutex> lock{mutex_};
                        wake_.wait_until(lock, nextSampleAt,
                                         [this]() noexcept { return stopRequested_.load(std::memory_order_acquire); });
                    }
                }
                catch (const std::exception& exception)
                {
                    SetError(std::string{"World Host process sampling worker failed: "} + exception.what());
                }
                catch (...)
                {
                    SetError("World Host process sampling worker failed with an unknown exception");
                }
            }

            [[nodiscard]] bool AppendSample(const BenchmarkServerProcessSampleV1& sample)
            {
                const BenchmarkWorldMeasurementPhase phase = phase_.load(std::memory_order_acquire);
                samples_.push_back(BenchmarkWorldProcessSampleV1{phase, sample});

                JsonObject document = JsonObject::object();
                document["schema"] = ProcessSampleSchema;
                document["version"] = ProcessSampleVersion;
                document["runId"] = runId_;
                document["phase"] = BenchmarkWorldArtifactSummary::PhaseName(phase);
                document["sequence"] = sample.sequence;
                document["elapsedNanoseconds"] = sample.elapsedNanoseconds;
                document["kernelTime100Nanoseconds"] = sample.kernelTime100Nanoseconds;
                document["userTime100Nanoseconds"] = sample.userTime100Nanoseconds;
                document["workingSetBytes"] = sample.workingSetBytes;
                document["privateUsageBytes"] = sample.privateUsageBytes;
                document["logicalProcessorCount"] = sample.logicalProcessorCount;
                document["hasIntervalCpuPercent"] = sample.hasIntervalCpuPercent;
                if (sample.hasIntervalCpuPercent)
                {
                    document["intervalCpuPercent"] = sample.intervalCpuPercent;
                }

                output_ << document.dump() << '\n';
                output_.flush();
                if (!output_.good())
                {
                    SetError("failed to write World Host process sample artifact");
                    return false;
                }
                return true;
            }

            void SetError(std::string error)
            {
                std::lock_guard<std::mutex> lock{mutex_};
                if (error_.empty())
                {
                    error_ = std::move(error);
                }
            }

            BenchmarkNativeProcessHandle processHandle_ = nullptr;
            std::string runId_;
            std::chrono::milliseconds interval_{0};
            std::ofstream output_;
            std::vector<BenchmarkWorldProcessSampleV1> samples_;
            std::thread worker_;
            std::atomic<bool> stopRequested_{false};
            std::atomic<BenchmarkWorldMeasurementPhase> phase_{BenchmarkWorldMeasurementPhase::Admission};
            std::condition_variable wake_;
            std::mutex mutex_;
            std::string error_;
        };

        [[nodiscard]] int Fail(const std::string& error)
        {
            std::cerr << "[world-host-controller] " << error << '\n';
            return ControllerFailureExitCode;
        }

        [[nodiscard]] std::string WriteEffectiveConfig(const std::filesystem::path& runsRoot,
                                                       const std::string_view runId,
                                                       const std::string_view normalizedConfigJson)
        {
            try
            {
                if (runId.empty() || normalizedConfigJson.empty())
                {
                    return "World Host benchmark effective config identity is empty";
                }

                JsonObject normalizedConfig = JsonObject::parse(normalizedConfigJson, nullptr, false);
                if (normalizedConfig.is_discarded() || !normalizedConfig.is_object())
                {
                    return "World Host benchmark normalized config JSON is invalid";
                }

                std::error_code filesystemError;
                const std::filesystem::path runDirectory = runsRoot / runId;
                if (!std::filesystem::is_directory(runDirectory, filesystemError) || filesystemError)
                {
                    return "World Host run directory is unavailable for effective config";
                }

                const std::filesystem::path benchmarkDirectory = runDirectory / "benchmark";
                std::filesystem::create_directory(benchmarkDirectory, filesystemError);
                if (filesystemError)
                {
                    return "failed to create World Host benchmark artifact directory";
                }

                const psnr::logging::ApplicationBuildInfo buildInfo = psnr::logging::ApplicationBuildInfo::Current();
                JsonObject executable = JsonObject::object();
                executable["benchmarkVersion"] = 1;
                executable["buildConfiguration"] = buildInfo.configuration;
                executable["architecture"] = buildInfo.architecture;
                executable["msvcVersion"] = buildInfo.msvcVersion;
                executable["msvcFullVersion"] = buildInfo.msvcFullVersion;

                JsonObject document = JsonObject::object();
                document["schema"] = EffectiveConfigSchema;
                document["version"] = EffectiveConfigVersion;
                document["runId"] = runId;
                document["executable"] = std::move(executable);
                document["controllerConfig"] = std::move(normalizedConfig);

                std::ofstream output{benchmarkDirectory / "effective-config.json", std::ios::binary | std::ios::trunc};
                if (!output.is_open())
                {
                    return "failed to open World Host benchmark effective config artifact";
                }
                output << document.dump(2) << '\n';
                output.flush();
                if (!output.good())
                {
                    return "failed to write World Host benchmark effective config artifact";
                }
                return {};
            }
            catch (const std::exception& exception)
            {
                return std::string{"failed to create World Host benchmark effective config: "} + exception.what();
            }
            catch (...)
            {
                return "failed to create World Host benchmark effective config with an unknown error";
            }
        }

        [[nodiscard]] std::string WriteClientResult(const std::filesystem::path& runsRoot, const std::string_view runId,
                                                    const std::uint32_t requestedClientCount,
                                                    const BenchmarkWorldClientWorkload& workload,
                                                    const std::string_view failureOperation = {},
                                                    const std::string_view failureMessage = {})
        {
            try
            {
                if (runId.empty() || requestedClientCount == 0 || failureOperation.empty() != failureMessage.empty())
                {
                    return "World Host benchmark client result identity is invalid";
                }

                JsonObject errors = JsonObject::array();
                if (!failureMessage.empty())
                {
                    JsonObject error = JsonObject::object();
                    error["operation"] = failureOperation;
                    error["message"] = failureMessage;
                    errors.push_back(std::move(error));
                }

                JsonObject document = JsonObject::object();
                document["schema"] = ClientResultSchema;
                document["version"] = ClientResultVersion;
                document["runId"] = runId;
                document["requestedClientCount"] = requestedClientCount;
                document["joinedClientCount"] = workload.JoinedClientCount();
                document["completedClientCount"] = workload.CompletedClientCount();
                document["sentControlCommandCount"] = workload.SentControlCommandCount();
                document["errorCount"] = errors.size();
                document["errors"] = std::move(errors);

                const std::filesystem::path path = runsRoot / runId / "benchmark" / "clients.jsonl";
                std::ofstream output{path, std::ios::binary | std::ios::trunc};
                if (!output.is_open())
                {
                    return "failed to open World Host benchmark client result artifact";
                }
                output << document.dump() << '\n';
                output.flush();
                if (!output.good())
                {
                    return "failed to write World Host benchmark client result artifact";
                }
                return {};
            }
            catch (const std::exception& exception)
            {
                return std::string{"failed to create World Host benchmark client result: "} + exception.what();
            }
            catch (...)
            {
                return "failed to create World Host benchmark client result with an unknown error";
            }
        }

        [[nodiscard]] int FailWithClientResult(const std::string& error, const std::string_view operation,
                                               const BenchmarkWorldHostControllerConfigV1& config,
                                               const std::string_view runId,
                                               const BenchmarkWorldClientWorkload& workload)
        {
            const std::string artifactError =
                WriteClientResult(config.runsRoot, runId, config.clientCount, workload, operation, error);
            if (!artifactError.empty())
            {
                return Fail(error + "; client artifact failure: " + artifactError);
            }
            return Fail(error);
        }

        [[nodiscard]] bool ReadEvent(BenchmarkServerChildProcess& child, const std::string& expectedRunId,
                                     BenchmarkIpcEventV1* const outEvent, std::string* const outError)
        {
            if (outEvent == nullptr || outError == nullptr)
            {
                return false;
            }

            std::string line;
            const BenchmarkIpcReadResult readResult = child.EventReader().ReadLine(&line);
            if (!readResult.Succeeded())
            {
                *outError = readResult.error;
                return false;
            }
            if (readResult.outcome == BenchmarkIpcReadOutcome::EndOfStream)
            {
                *outError = "World Host event pipe ended before the expected lifecycle event";
                return false;
            }

            const BenchmarkIpcEventDecodeResult decodeResult = BenchmarkIpcEventV1Codec::Decode(line);
            if (!decodeResult.Succeeded())
            {
                *outError = decodeResult.error;
                return false;
            }
            if (decodeResult.event.runId != expectedRunId)
            {
                *outError = "World Host event runId does not match Controller runId";
                return false;
            }
            if (decodeResult.event.type == BenchmarkIpcEventType::Error)
            {
                *outError = decodeResult.event.errorMessage;
                return false;
            }

            *outEvent = decodeResult.event;
            return true;
        }
    } // namespace

    int BenchmarkWorldHostController::Run(const BenchmarkWorldHostControllerConfigV1& config,
                                          const std::string_view normalizedConfigJson)
    {
        std::string runId;
        if (!BenchmarkRunArtifactFactory::TryCreateRunId(&runId))
        {
            return Fail("failed to create World Host runId");
        }

        BenchmarkServerChildLaunchResult launchResult = BenchmarkServerChildProcess::LaunchWorldHost(
            config.executablePath, runId, config.serverConfigPath, config.runsRoot);
        if (!launchResult.Succeeded())
        {
            return Fail(launchResult.error);
        }

        BenchmarkServerChildProcess& child = *launchResult.child;
        BenchmarkIpcEventV1 event;
        std::string error;
        if (!ReadEvent(child, runId, &event, &error))
        {
            return Fail(error);
        }
        if (event.type != BenchmarkIpcEventType::Ready || event.sequence != 0)
        {
            return Fail("World Host did not publish the expected ready event");
        }
        std::cout << "[world-host-controller] ready: runId=" << runId << '\n';

        const std::string effectiveConfigError = WriteEffectiveConfig(config.runsRoot, runId, normalizedConfigJson);
        if (!effectiveConfigError.empty())
        {
            return Fail(effectiveConfigError);
        }

        BenchmarkWorldHostProcessSamplingWorker processSamplingWorker;
        const std::string processSamplingStartError = processSamplingWorker.Start(
            child.ProcessHandle(), config.runsRoot, runId, config.samplingIntervalMilliseconds);
        if (!processSamplingStartError.empty())
        {
            return Fail(processSamplingStartError);
        }

        BenchmarkWorldClientWorkload workload;
        psnr::runtime::NrEndpoint endpoint;
        if (!BenchmarkEndpointParser::TryParseServerEndpoint(config.clientAddress, config.clientPort, &endpoint))
        {
            return FailWithClientResult(
                "clients.address must be a dotted-decimal IPv4 address and clients.port must be valid", "admission",
                config, runId, workload);
        }

        const std::string controlWorkloadError =
            workload.StartControlWorkload(config.workloadSeed, config.controlIntervalMilliseconds, config.boostPercent);
        if (!controlWorkloadError.empty())
        {
            return FailWithClientResult(controlWorkloadError, "controlWorkload", config, runId, workload);
        }
        psnr::runtime::NrClientConfig clientConfig;
        clientConfig.eventQueueCapacity = config.clientEventQueueCapacity;
        clientConfig.payloadQueueCapacity = config.clientPayloadQueueCapacity;
        const std::string admissionError =
            workload.StartAdmission(endpoint, clientConfig, config.clientCount, config.clientRampPerSecond,
                                    config.clientAdmissionTimeoutMilliseconds);
        if (!admissionError.empty())
        {
            return FailWithClientResult(admissionError, "admission", config, runId, workload);
        }
        std::cout << "[world-host-controller] clients joined: count=" << workload.JoinedClientCount() << '\n';
        processSamplingWorker.BeginMeasurement();

        const std::string roundResultError = workload.WaitForRoundResults(config.clientRoundResultTimeoutMilliseconds);
        if (!roundResultError.empty())
        {
            return FailWithClientResult(roundResultError, "roundResult", config, runId, workload);
        }
        processSamplingWorker.EndMeasurement();
        std::cout << "[world-host-controller] round results committed: count=" << workload.CompletedClientCount()
                  << '\n';
        std::cout << "[world-host-controller] control commands sent: count=" << workload.SentControlCommandCount()
                  << '\n';

        const std::string drainStopError = workload.StopEventDrain();
        if (!drainStopError.empty())
        {
            return FailWithClientResult(drainStopError, "eventDrain", config, runId, workload);
        }
        const std::string clientResultError = WriteClientResult(config.runsRoot, runId, config.clientCount, workload);
        if (!clientResultError.empty())
        {
            return Fail(clientResultError);
        }
        const std::string processSamplingStopError = processSamplingWorker.Stop();
        if (!processSamplingStopError.empty())
        {
            return Fail(processSamplingStopError);
        }
        const BenchmarkWorldArtifactSummaryWriteResult processSummaryResult =
            BenchmarkWorldArtifactSummary::WriteMeasurementProcessSummary(config.runsRoot, runId,
                                                                          processSamplingWorker.Samples());
        if (!processSummaryResult.Succeeded())
        {
            return Fail(processSummaryResult.error);
        }

        BenchmarkIpcCommandV1 stopCommand;
        stopCommand.type = BenchmarkIpcCommandType::Stop;
        stopCommand.runId = runId;
        stopCommand.sequence = StopCommandSequence;
        const BenchmarkIpcCommandEncodeResult encodeResult = BenchmarkIpcCommandV1Codec::Encode(stopCommand);
        if (!encodeResult.Succeeded())
        {
            return Fail(encodeResult.error);
        }

        const BenchmarkIpcIoResult writeResult = child.CommandWriter().WriteLine(encodeResult.json);
        if (!writeResult.Succeeded())
        {
            return Fail(writeResult.error);
        }
        if (!ReadEvent(child, runId, &event, &error))
        {
            return Fail(error);
        }
        if (event.type != BenchmarkIpcEventType::Stopped || event.sequence != StopCommandSequence)
        {
            return Fail("World Host did not publish the expected stopped event");
        }

        const BenchmarkServerChildExitResult exitResult = child.WaitForExit(config.shutdownTimeoutMilliseconds);
        if (!exitResult.Succeeded())
        {
            return Fail(exitResult.error);
        }
        if (exitResult.exitCode != 0)
        {
            return Fail("World Host returned a non-zero exit code");
        }

        const std::string workloadShutdownError = workload.Shutdown();
        if (!workloadShutdownError.empty())
        {
            return FailWithClientResult(workloadShutdownError, "shutdown", config, runId, workload);
        }

        const BenchmarkWorldMergedArtifactWriteResult mergedResult =
            BenchmarkWorldMergedArtifact::Write(config.runsRoot, runId);
        if (!mergedResult.Succeeded())
        {
            return Fail(mergedResult.error);
        }
        if (!mergedResult.valid)
        {
            return Fail("World benchmark merged verdict is invalid");
        }

        std::cout << "[world-host-controller] stopped: runId=" << runId << " exitCode=" << exitResult.exitCode << '\n';
        return ControllerSuccessExitCode;
    }
} // namespace psnr::benchmark
