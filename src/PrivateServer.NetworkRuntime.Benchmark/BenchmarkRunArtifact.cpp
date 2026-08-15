#include "BenchmarkRunArtifact.h"

#include "ApplicationBuildInfo.h"
#include "BenchmarkProtocol.h"
#include "BenchmarkStatistics.h"

#include <PrivateServer/NetworkRuntime/Version.h>

#define NOMINMAX
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <objbase.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::string_view ArtifactSchemaName = "psnr.network_runtime.benchmark.run";
        constexpr std::uint64_t ArtifactSchemaVersion = 1;
        constexpr std::string_view ConfigSchemaName = "psnr.network_runtime.benchmark.config";
        constexpr std::uint64_t ConfigSchemaVersion = 1;
        constexpr std::string_view RunMetadataType = "runMetadata";
        constexpr std::string_view PeriodicSampleType = "periodicSample";
        constexpr std::string_view ServeSummaryType = "serveSummary";
        constexpr std::string_view WorkloadOutcomeType = "workloadOutcome";
        constexpr std::string_view LatencySamplesType = "latencySamples";
        constexpr std::string_view LatencySummaryType = "latencySummary";
        constexpr std::string_view MeasurementProcessSummaryType = "measurementProcessSummary";
        constexpr std::string_view RunSummaryType = "runSummary";
        constexpr std::string_view ApplicationObservedRttMetric = "applicationObservedRtt";
        constexpr std::string_view ServerProcessingDurationMetric = "serverProcessingDuration";
        constexpr std::string_view SchedulerLagMetric = "schedulerLag";
        constexpr std::string_view NanosecondsUnit = "nanoseconds";
        constexpr std::size_t LatencySampleChunkSize = 4'096;

        using JsonObject = nlohmann::ordered_json;

        [[nodiscard]] BenchmarkRunArtifactCreateResult Failure(std::string error)
        {
            BenchmarkRunArtifactCreateResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] BenchmarkRunArtifactWriteResult WriteFailure(std::string error)
        {
            BenchmarkRunArtifactWriteResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] std::string FormatUtcNow() // UTC ISO-8601 시간 생성
        {
            SYSTEMTIME time{};
            GetSystemTime(&time);
            std::array<char, 25> text{};
            const int written = std::snprintf(
                text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", static_cast<unsigned int>(time.wYear),
                static_cast<unsigned int>(time.wMonth), static_cast<unsigned int>(time.wDay),
                static_cast<unsigned int>(time.wHour), static_cast<unsigned int>(time.wMinute),
                static_cast<unsigned int>(time.wSecond), static_cast<unsigned int>(time.wMilliseconds));
            return written > 0 ? std::string(text.data(), static_cast<std::size_t>(written)) : std::string{};
        }

        // normalized config를 FNV-1a 64-bit로 hash
        [[nodiscard]] std::string HashNormalizedConfig(const std::string_view normalizedConfigJson)
        {
            std::uint64_t hash = 14'695'981'039'346'656'037ULL;
            for (const unsigned char byte : normalizedConfigJson)
            {
                hash ^= byte;
                hash *= 1'099'511'628'211ULL;
            }

            std::array<char, 17> text{};
            const int written =
                std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(hash));
            return written == 16 ? std::string(text.data(), 16) : std::string{};
        }

        [[nodiscard]] std::string WideToUtf8(const std::wstring_view text)
        {
            if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                return {};
            }
            const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                                     static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                return {};
            }
            std::string encoded(static_cast<std::size_t>(required), '\0');
            const int written =
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                    encoded.data(), required, nullptr, nullptr);
            return written == required ? encoded : std::string{};
        }

        // RtlGetVersion으로 Windows 버전 확인
        [[nodiscard]] std::string CaptureWindowsVersion()
        {
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (ntdll == nullptr)
            {
                return {};
            }
            using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
            const RtlGetVersionFunction rtlGetVersion =
                reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtlGetVersion == nullptr)
            {
                return {};
            }

            RTL_OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);
            if (rtlGetVersion(&version) != 0)
            {
                return {};
            }
            return std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
                   std::to_string(version.dwBuildNumber);
        }

        // Windows Registry에서 CPU 모델 확인
        [[nodiscard]] std::string CaptureCpuModel()
        {
            std::array<wchar_t, 256> value{};
            DWORD valueBytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
            const LSTATUS status =
                RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                             L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, value.data(), &valueBytes);
            if (status != ERROR_SUCCESS || valueBytes < sizeof(wchar_t))
            {
                return {};
            }
            const std::size_t characterCount = valueBytes / sizeof(wchar_t);
            const std::size_t textLength =
                characterCount > 0 && value[characterCount - 1] == L'\0' ? characterCount - 1 : characterCount;
            return WideToUtf8(std::wstring_view(value.data(), textLength));
        }

        [[nodiscard]] BenchmarkRunMetadataV1 CaptureRunMetadata(const std::string_view normalizedConfigJson,
                                                                const std::string_view baselineSetId,
                                                                const std::uint32_t repeatIndex,
                                                                const std::uint32_t repeatCount)
        {
            BenchmarkRunMetadataV1 metadata;
            metadata.baselineSetId = baselineSetId;
            metadata.startedAtUtc = FormatUtcNow();
            metadata.configHash = HashNormalizedConfig(normalizedConfigJson);
            metadata.windowsVersion = CaptureWindowsVersion();
            metadata.cpuModel = CaptureCpuModel();
            metadata.repeatIndex = repeatIndex;
            metadata.repeatCount = repeatCount;
            metadata.logicalProcessorCount = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); // 전체 논리 프로세스 수

            MEMORYSTATUSEX memoryStatus{};
            memoryStatus.dwLength = sizeof(memoryStatus);
            if (GlobalMemoryStatusEx(&memoryStatus) != FALSE) // 전체 물리 메모리 확인
            {
                metadata.totalPhysicalMemoryBytes = memoryStatus.ullTotalPhys;
            }
            return metadata;
        }

        [[nodiscard]] JsonObject EncodeMetadata(const BenchmarkRunArtifact& artifact, JsonObject&& observation)
        {
            JsonObject config = JsonObject::object();
            config["schema"] = ConfigSchemaName;
            config["version"] = ConfigSchemaVersion;
            config["hashAlgorithm"] = "fnv1a64";
            config["hash"] = artifact.metadata.configHash;

            JsonObject executable = JsonObject::object();
            executable["benchmarkVersion"] = 1;
            executable["runtimeAbiVersion"] = nr_get_version();
            const psnr::logging::ApplicationBuildInfo buildInfo = psnr::logging::ApplicationBuildInfo::Current();
            executable["buildConfiguration"] = buildInfo.configuration;
            executable["architecture"] = buildInfo.architecture;
            executable["msvcVersion"] = buildInfo.msvcVersion;
            executable["msvcFullVersion"] = buildInfo.msvcFullVersion;
            executable["sourceRevisionAvailable"] = false;

            JsonObject environment = JsonObject::object();
            environment["windowsVersion"] = artifact.metadata.windowsVersion;
            environment["cpuModel"] = artifact.metadata.cpuModel;
            environment["logicalProcessorCount"] = artifact.metadata.logicalProcessorCount;
            environment["totalPhysicalMemoryBytes"] = artifact.metadata.totalPhysicalMemoryBytes;

            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = RunMetadataType;
            document["runId"] = artifact.runId;
            document["baselineSetId"] = artifact.metadata.baselineSetId;
            document["repeatIndex"] = artifact.metadata.repeatIndex;
            document["repeatCount"] = artifact.metadata.repeatCount;
            document["startedAtUtc"] = artifact.metadata.startedAtUtc;
            document["config"] = std::move(config);
            document["executable"] = std::move(executable);
            document["environment"] = std::move(environment);
            document["observation"] = std::move(observation);
            return document;
        }

        [[nodiscard]] JsonObject EncodeRunMetadata(const BenchmarkRunArtifact& artifact)
        {
            JsonObject observation = JsonObject::object();
            observation["benchmarkProtocolVersion"] = BenchmarkProtocolVersion;
            observation["trafficProducerMode"] = "controller-native-client-workers";
            observation["responseObserverMode"] = "controller-worker-round-robin-drain";
            observation["applicationObservedRttStart"] = "client-steady-ns-before-payload-encode";
            observation["applicationObservedRttEnd"] = "client-steady-ns-at-event-drain";
            observation["activeDrainProbeBudget"] = BenchmarkActiveDrainProbeBudget;
            observation["eventPollIntervalMilliseconds"] = BenchmarkEventPollIntervalMilliseconds;
            observation["runtimeSnapshotMode"] = "controller-interval-ipc";
            observation["processResourceMode"] = "controller-interval-win32-process-handle";
            observation["diagnosticsMode"] = "benchmark-jsonl";
            return EncodeMetadata(artifact, std::move(observation));
        }

        [[nodiscard]] JsonObject EncodeServeMetadata(const BenchmarkRunArtifact& artifact)
        {
            JsonObject observation = JsonObject::object();
            observation["benchmarkProtocolVersion"] = BenchmarkProtocolVersion;
            observation["trafficProducerMode"] = "external-client";
            observation["responseObserverMode"] = "external-client-owned";
            observation["runtimeSnapshotMode"] = "controller-interval-ipc";
            observation["processResourceMode"] = "controller-interval-win32-process-handle";
            observation["diagnosticsMode"] = "benchmark-jsonl";
            observation["serveSampleWriteMode"] = "controller-interval-jsonl-flush";
            JsonObject document = EncodeMetadata(artifact, std::move(observation));
            document.erase("baselineSetId");
            document.erase("repeatIndex");
            document.erase("repeatCount");
            return document;
        }

        [[nodiscard]] std::string_view PhaseToken(const BenchmarkWorkloadPhase phase) noexcept
        {
            switch (phase)
            {
            case BenchmarkWorkloadPhase::Connect:
                return "connect";
            case BenchmarkWorkloadPhase::Warmup:
                return "warmup";
            case BenchmarkWorkloadPhase::Measurement:
                return "measurement";
            case BenchmarkWorkloadPhase::Drain:
                return "drain";
            }

            return {};
        }

        [[nodiscard]] std::string_view LifecycleStateToken(
            const psnr::runtime::NrServerLifecycleState lifecycleState) noexcept
        {
            switch (lifecycleState)
            {
            case psnr::runtime::NrServerLifecycleState::Created:
                return "created";
            case psnr::runtime::NrServerLifecycleState::Running:
                return "running";
            case psnr::runtime::NrServerLifecycleState::StopRequested:
                return "stopRequested";
            case psnr::runtime::NrServerLifecycleState::Shutdown:
                return "shutdown";
            case psnr::runtime::NrServerLifecycleState::Invalid:
                return {};
            }

            return {};
        }

        [[nodiscard]] JsonObject EncodeProcessSample(const BenchmarkServerProcessSampleV1& sample)
        {
            JsonObject document = JsonObject::object();
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
            return document;
        }

        [[nodiscard]] JsonObject EncodeRuntimeSample(const BenchmarkRuntimeSampleV1& sample)
        {
            JsonObject document = JsonObject::object();
            document["lifecycleState"] = std::string(LifecycleStateToken(sample.lifecycleState));
            document["registeredSessionCount"] = sample.registeredSessionCount;
            document["closingSessionCount"] = sample.closingSessionCount;
            document["pendingRecvIoCount"] = sample.pendingRecvIoCount;
            document["pendingSendIoCount"] = sample.pendingSendIoCount;
            document["toWorldEventDepth"] = sample.toWorldEventDepth;
            document["toWorldEventHighWatermark"] = sample.toWorldEventHighWatermark;
            document["totalPressureTransactions"] = sample.totalPressureTransactions;

            JsonObject pressureCounts = JsonObject::array();
            for (const std::uint64_t count : sample.pressureTransactionCounts)
            {
                pressureCounts.push_back(count);
            }
            document["pressureTransactionCounts"] = std::move(pressureCounts);

            JsonObject poolExhaustionCounts = JsonObject::array();
            for (const std::uint64_t count : sample.poolExhaustionCounts)
            {
                poolExhaustionCounts.push_back(count);
            }
            document["poolExhaustionCounts"] = std::move(poolExhaustionCounts);

            JsonObject memoryPools = JsonObject::array();
            for (const psnr::runtime::NrServerMemoryPoolSnapshot& pool : sample.memoryPools)
            {
                JsonObject poolDocument = JsonObject::object();
                poolDocument["capacity"] = pool.capacity;
                poolDocument["inUse"] = pool.inUse;
                poolDocument["available"] = pool.available;
                poolDocument["highWatermark"] = pool.highWatermark;
                memoryPools.push_back(std::move(poolDocument));
            }
            document["memoryPools"] = std::move(memoryPools);

            JsonObject diagnostics = JsonObject::object();
            diagnostics["enabled"] = sample.diagnostics.enabled;
            diagnostics["sinkFailed"] = sample.diagnostics.sinkFailed;
            diagnostics["attempted"] = sample.diagnostics.attempted;
            diagnostics["enqueued"] = sample.diagnostics.enqueued;
            diagnostics["droppedQueueFull"] = sample.diagnostics.droppedQueueFull;
            diagnostics["droppedSinkUnavailable"] = sample.diagnostics.droppedSinkUnavailable;
            diagnostics["consumed"] = sample.diagnostics.consumed;
            diagnostics["discardedAfterSinkFailure"] = sample.diagnostics.discardedAfterSinkFailure;
            document["diagnostics"] = std::move(diagnostics);
            return document;
        }

        [[nodiscard]] JsonObject EncodePeriodicSample(const BenchmarkRunArtifact& artifact,
                                                      const BenchmarkPeriodicSampleV1& sample)
        {
            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = PeriodicSampleType;
            document["runId"] = artifact.runId;
            document["sequence"] = sample.sequence;
            document["phase"] = PhaseToken(sample.phase);
            document["processSample"] = EncodeProcessSample(sample.process);
            document["runtimeSample"] = EncodeRuntimeSample(sample.runtime);
            return document;
        }

        [[nodiscard]] JsonObject EncodeServePeriodicSample(const std::string_view runId, const std::uint64_t sequence,
                                                           const BenchmarkServerProcessSampleV1& processSample,
                                                           const BenchmarkRuntimeSampleV1& runtimeSample)
        {
            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = PeriodicSampleType;
            document["runId"] = runId;
            document["sequence"] = sequence;
            document["phase"] = "serve";
            document["processSample"] = EncodeProcessSample(processSample);
            document["runtimeSample"] = EncodeRuntimeSample(runtimeSample);
            return document;
        }

        [[nodiscard]] JsonObject EncodeWorkloadOutcome(const BenchmarkRunArtifact& artifact,
                                                       const BenchmarkClientWorkloadResult& workloadResult)
        {
            const std::uint64_t planned = workloadResult.accepted + workloadResult.missedSchedule;
            const double scheduleFulfillmentRatio =
                planned == 0 ? 1.0 : static_cast<double>(workloadResult.accepted) / static_cast<double>(planned);
            const bool valid = workloadResult.error.empty() && workloadResult.sendRejected == 0 &&
                               workloadResult.timeout == 0 && workloadResult.unexpectedDisconnect == 0 &&
                               workloadResult.accepted == workloadResult.completed;

            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = WorkloadOutcomeType;
            document["runId"] = artifact.runId;
            document["valid"] = valid;
            document["planned"] = planned;
            document["accepted"] = workloadResult.accepted;
            document["completed"] = workloadResult.completed;
            document["missedSchedule"] = workloadResult.missedSchedule;
            document["sendRejected"] = workloadResult.sendRejected;
            document["timeout"] = workloadResult.timeout;
            document["unexpectedDisconnect"] = workloadResult.unexpectedDisconnect;
            document["scheduleFulfillmentRatio"] = scheduleFulfillmentRatio;
            if (!workloadResult.error.empty())
            {
                document["error"] = workloadResult.error;
            }
            return document;
        }

        [[nodiscard]] JsonObject EncodeLatencySamples(const BenchmarkRunArtifact& artifact,
                                                      const std::string_view metric, const std::size_t chunkIndex,
                                                      const std::span<const std::int64_t> samples)
        {
            JsonObject values = JsonObject::array();
            for (const std::int64_t sample : samples)
            {
                values.push_back(sample);
            }

            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = LatencySamplesType;
            document["runId"] = artifact.runId;
            document["metric"] = metric;
            document["unit"] = NanosecondsUnit;
            document["chunkIndex"] = chunkIndex;
            document["sampleCount"] = samples.size();
            document["values"] = std::move(values);
            return document;
        }

        [[nodiscard]] JsonObject EncodeLatencySummary(const BenchmarkRunArtifact& artifact,
                                                      const std::string_view metric,
                                                      const std::span<const std::int64_t> samples)
        {
            const std::optional<BenchmarkLatencyStatisticsV1> statistics =
                BenchmarkStatistics::SummarizeLatency(samples);

            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = LatencySummaryType;
            document["runId"] = artifact.runId;
            document["metric"] = metric;
            document["unit"] = NanosecondsUnit;
            document["algorithm"] = BenchmarkStatistics::PercentileAlgorithm;
            document["sampleCount"] = statistics->sampleCount;
            document["min"] = statistics->minimum;
            document["mean"] = statistics->mean;
            document["p50"] = statistics->p50;
            document["p95"] = statistics->p95;
            document["p99"] = statistics->p99;
            document["max"] = statistics->maximum;
            return document;
        }

        [[nodiscard]] std::optional<JsonObject> EncodeMeasurementProcessSummary(
            const BenchmarkRunArtifact& artifact, const std::span<const BenchmarkPeriodicSampleV1> periodicSamples)
        {
            std::vector<BenchmarkServerProcessSampleV1> samples;
            for (const BenchmarkPeriodicSampleV1& periodicSample : periodicSamples)
            {
                if (periodicSample.phase == BenchmarkWorkloadPhase::Measurement)
                {
                    samples.push_back(periodicSample.process);
                }
            }
            const std::optional<BenchmarkProcessStatisticsV1> statistics =
                BenchmarkStatistics::SummarizeProcess(samples);
            if (!statistics.has_value())
            {
                return std::nullopt;
            }

            JsonObject cpu = JsonObject::object();
            cpu["logicalProcessorCount"] = statistics->cpu.logicalProcessorCount;
            cpu["hasOverallAveragePercent"] = statistics->cpu.hasOverallAveragePercent;
            if (statistics->cpu.hasOverallAveragePercent)
            {
                cpu["overallAveragePercent"] = statistics->cpu.overallAveragePercent;
                cpu["observedIntervalNanoseconds"] = statistics->cpu.observedIntervalNanoseconds;
            }
            cpu["intervalSampleCount"] = statistics->cpu.intervalSampleCount;
            if (statistics->cpu.hasIntervalStatistics)
            {
                cpu["intervalP95Percent"] = statistics->cpu.intervalP95Percent;
                cpu["intervalMaxPercent"] = statistics->cpu.intervalMaximumPercent;
            }

            JsonObject workingSet = JsonObject::object();
            workingSet["sampledStartBytes"] = statistics->memory.workingSetStartBytes;
            workingSet["sampledEndBytes"] = statistics->memory.workingSetEndBytes;
            workingSet["sampledPeakBytes"] = statistics->memory.workingSetPeakBytes;

            JsonObject privateUsage = JsonObject::object();
            privateUsage["sampledStartBytes"] = statistics->memory.privateUsageStartBytes;
            privateUsage["sampledEndBytes"] = statistics->memory.privateUsageEndBytes;
            privateUsage["sampledPeakBytes"] = statistics->memory.privateUsagePeakBytes;
            privateUsage["sampledDeltaBytes"] = statistics->memory.privateUsageDeltaBytes;

            JsonObject memory = JsonObject::object();
            memory["workingSet"] = std::move(workingSet);
            memory["privateUsage"] = std::move(privateUsage);

            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = MeasurementProcessSummaryType;
            document["runId"] = artifact.runId;
            document["phase"] = "measurement";
            document["sampleCount"] = statistics->sampleCount;
            document["firstSequence"] = statistics->firstSequence;
            document["lastSequence"] = statistics->lastSequence;
            document["cpu"] = std::move(cpu);
            document["memory"] = std::move(memory);
            return document;
        }

        [[nodiscard]] JsonObject EncodeRunSummary(const BenchmarkRunArtifact& artifact,
                                                  const BenchmarkRunCompletionV1& completion)
        {
            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = RunSummaryType;
            document["runId"] = artifact.runId;
            document["baselineSetId"] = artifact.metadata.baselineSetId;
            document["repeatIndex"] = artifact.metadata.repeatIndex;
            document["repeatCount"] = artifact.metadata.repeatCount;
            document["completedAtUtc"] = FormatUtcNow();
            document["terminal"] = true;
            document["valid"] = completion.valid;
            document["outcome"] = completion.valid ? "valid" : "invalid";
            if (!completion.error.empty())
            {
                document["error"] = completion.error;
            }
            return document;
        }

        [[nodiscard]] JsonObject EncodeServeSummary(const std::string_view runId, const std::uint64_t sampleCount)
        {
            JsonObject document = JsonObject::object();
            document["schema"] = ArtifactSchemaName;
            document["version"] = ArtifactSchemaVersion;
            document["type"] = ServeSummaryType;
            document["runId"] = runId;
            document["completedAtUtc"] = FormatUtcNow();
            document["terminal"] = true;
            document["graceful"] = true;
            document["sampleCount"] = sampleCount;
            return document;
        }

        [[nodiscard]] BenchmarkRunArtifactWriteResult WriteDocument(const JsonObject& document,
                                                                    std::ofstream* const output)
        {
            const std::string serialized = document.dump();
            output->write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
            output->put('\n');
            if (!output->good())
            {
                return WriteFailure("failed to write benchmark JSONL record");
            }
            return {};
        }

        [[nodiscard]] BenchmarkRunArtifactWriteResult WriteLatencyEvidence(const BenchmarkRunArtifact& artifact,
                                                                           const std::string_view metric,
                                                                           const std::span<const std::int64_t> samples,
                                                                           std::ofstream* const output)
        {
            for (std::size_t offset = 0; offset < samples.size(); offset += LatencySampleChunkSize)
            {
                const std::size_t chunkSize = (std::min)(LatencySampleChunkSize, samples.size() - offset);
                const std::span<const std::int64_t> chunk = samples.subspan(offset, chunkSize);
                const BenchmarkRunArtifactWriteResult chunkResult = WriteDocument(
                    EncodeLatencySamples(artifact, metric, offset / LatencySampleChunkSize, chunk), output);

                if (!chunkResult.Succeeded())
                {
                    return chunkResult;
                }
            }

            if (!samples.empty())
            {
                return WriteDocument(EncodeLatencySummary(artifact, metric, samples), output);
            }
            return {};
        }

        [[nodiscard]] bool TryCreateIdentifier(const std::string_view prefix, std::string* const outIdentifier)
        {
            if (prefix.empty() || outIdentifier == nullptr)
            {
                return false;
            }
            GUID guid{};
            if (FAILED(CoCreateGuid(&guid)))
            {
                return false;
            }

            wchar_t guidText[39]{};
            if (StringFromGUID2(guid, guidText, static_cast<int>(std::size(guidText))) == 0)
            {
                return false;
            }

            std::string identifier(prefix);
            identifier.reserve(prefix.size() + 36);
            for (std::size_t index = 1; index < 37; ++index)
            {
                identifier.push_back(static_cast<char>(guidText[index]));
            }
            *outIdentifier = std::move(identifier);
            return true;
        }

        [[nodiscard]] bool TryCreateCanonicalRunId(std::string* const outRunId)
        {
            if (outRunId == nullptr)
            {
                return false;
            }

            SYSTEMTIME time{};
            GetSystemTime(&time);

            std::array<char, 21> timestampText{};
            const int timestampLength =
                std::snprintf(timestampText.data(), timestampText.size(), "%04u%02u%02uT%02u%02u%02u.%03uZ",
                              static_cast<unsigned int>(time.wYear), static_cast<unsigned int>(time.wMonth),
                              static_cast<unsigned int>(time.wDay), static_cast<unsigned int>(time.wHour),
                              static_cast<unsigned int>(time.wMinute), static_cast<unsigned int>(time.wSecond),
                              static_cast<unsigned int>(time.wMilliseconds));
            if (timestampLength != 20)
            {
                return false;
            }

            GUID guid{};
            if (FAILED(CoCreateGuid(&guid)))
            {
                return false;
            }

            std::array<wchar_t, 39> guidText{};
            if (StringFromGUID2(guid, guidText.data(), static_cast<int>(guidText.size())) == 0)
            {
                return false;
            }

            std::string runId = "run-";
            runId.append(timestampText.data(), static_cast<std::size_t>(timestampLength));
            runId.push_back('-');
            runId.reserve(61);
            for (std::size_t index = 1; index < 37; ++index)
            {
                const unsigned char character = static_cast<unsigned char>(guidText[index]);
                runId.push_back(static_cast<char>(std::tolower(character)));
            }

            *outRunId = std::move(runId);
            return true;
        }
    } // namespace

    bool BenchmarkRunArtifactFactory::TryCreateBaselineSetId(std::string* const outBaselineSetId)
    {
        return TryCreateIdentifier("baseline-set-", outBaselineSetId);
    }

    bool BenchmarkRunArtifactFactory::TryCreateRunId(std::string* const outRunId)
    {
        return TryCreateCanonicalRunId(outRunId);
    }

    BenchmarkRunArtifactCreateResult BenchmarkRunArtifactFactory::Create(const std::string_view outputRoot,
                                                                         const std::string_view normalizedConfigJson,
                                                                         const std::string_view baselineSetId,
                                                                         const std::uint32_t repeatIndex,
                                                                         const std::uint32_t repeatCount)
    {
        if (outputRoot.empty() || normalizedConfigJson.empty() || baselineSetId.empty() || repeatIndex == 0 ||
            repeatIndex > repeatCount)
        {
            return Failure("run artifact baseline context, output root, or normalized config is invalid");
        }

        std::string runId;
        if (!TryCreateRunId(&runId))
        {
            return Failure("failed to create benchmark runId");
        }

        const std::u8string encodedOutputRoot(outputRoot.cbegin(), outputRoot.cend());
        const std::filesystem::path rootDirectory(encodedOutputRoot);
        std::error_code directoryError;
        std::filesystem::create_directories(rootDirectory, directoryError);
        if (directoryError)
        {
            return Failure("failed to create benchmark artifact output root: " + directoryError.message());
        }

        const std::filesystem::path runDirectory = rootDirectory / runId;
        const bool created = std::filesystem::create_directory(runDirectory, directoryError);
        if (directoryError || !created)
        {
            return Failure("failed to create unique benchmark run artifact directory");
        }

        const std::filesystem::path configPath = runDirectory / "config.json";      // path의 경로 결합 연산자 '/'
        std::ofstream configOutput(configPath, std::ios::binary | std::ios::trunc); // 출력 파일 open
        if (!configOutput.is_open())
        {
            return Failure("failed to open benchmark run config artifact");
        }

        configOutput.write(normalizedConfigJson.data(), static_cast<std::streamsize>(normalizedConfigJson.size()));
        configOutput.put('\n');
        configOutput.flush();
        if (!configOutput.good())
        {
            return Failure("failed to write benchmark run config artifact");
        }

        BenchmarkRunArtifactCreateResult result;
        result.artifact.runId = std::move(runId);
        result.artifact.directory = runDirectory;
        result.artifact.metadata = CaptureRunMetadata(normalizedConfigJson, baselineSetId, repeatIndex, repeatCount);
        if (result.artifact.metadata.startedAtUtc.empty() || result.artifact.metadata.configHash.empty())
        {
            return Failure("failed to capture required benchmark run metadata");
        }
        return result;
    }

    BenchmarkRunArtifactWriteResult BenchmarkRunArtifactWriter::WriteRunEvidence(
        const BenchmarkRunArtifact& artifact, const std::span<const BenchmarkPeriodicSampleV1> periodicSamples,
        const BenchmarkClientWorkloadResult& workloadResult, const BenchmarkRunCompletionV1& completion)
    {
        if (artifact.runId.empty() || artifact.directory.empty())
        {
            return WriteFailure("benchmark run artifact is invalid");
        }

        const std::filesystem::path benchmarkPath = artifact.directory / "benchmark.jsonl";
        std::ofstream output(benchmarkPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            return WriteFailure("failed to open benchmark JSONL artifact");
        }

        try
        {
            const BenchmarkRunArtifactWriteResult metadataResult = WriteDocument(EncodeRunMetadata(artifact), &output);
            if (!metadataResult.Succeeded())
            {
                return metadataResult;
            }

            for (const BenchmarkPeriodicSampleV1& sample : periodicSamples)
            {
                if (sample.sequence == 0 || sample.process.sequence != sample.sequence ||
                    PhaseToken(sample.phase).empty() || LifecycleStateToken(sample.runtime.lifecycleState).empty())
                {
                    return WriteFailure("periodic benchmark sample is invalid");
                }

                const BenchmarkRunArtifactWriteResult sampleResult =
                    WriteDocument(EncodePeriodicSample(artifact, sample), &output);
                if (!sampleResult.Succeeded())
                {
                    return sampleResult;
                }
            }

            const BenchmarkRunArtifactWriteResult outcomeResult =
                WriteDocument(EncodeWorkloadOutcome(artifact, workloadResult), &output);
            if (!outcomeResult.Succeeded())
            {
                return outcomeResult;
            }

            const BenchmarkRunArtifactWriteResult applicationRttResult = WriteLatencyEvidence(
                artifact, ApplicationObservedRttMetric, workloadResult.applicationObservedRttNanoseconds, &output);
            if (!applicationRttResult.Succeeded())
            {
                return applicationRttResult;
            }

            const BenchmarkRunArtifactWriteResult serverProcessingResult = WriteLatencyEvidence(
                artifact, ServerProcessingDurationMetric, workloadResult.serverProcessingDurationNanoseconds, &output);
            if (!serverProcessingResult.Succeeded())
            {
                return serverProcessingResult;
            }

            const BenchmarkRunArtifactWriteResult schedulerLagResult =
                WriteLatencyEvidence(artifact, SchedulerLagMetric, workloadResult.schedulerLagNanoseconds, &output);
            if (!schedulerLagResult.Succeeded())
            {
                return schedulerLagResult;
            }

            const std::optional<JsonObject> processSummary = EncodeMeasurementProcessSummary(artifact, periodicSamples);
            if (processSummary.has_value())
            {
                const BenchmarkRunArtifactWriteResult processSummaryResult = WriteDocument(*processSummary, &output);
                if (!processSummaryResult.Succeeded())
                {
                    return processSummaryResult;
                }
            }

            const BenchmarkRunArtifactWriteResult summaryResult =
                WriteDocument(EncodeRunSummary(artifact, completion), &output);
            if (!summaryResult.Succeeded())
            {
                return summaryResult;
            }
            output.flush();
            if (!output.good())
            {
                return WriteFailure("failed to flush benchmark JSONL artifact");
            }
        }
        catch (const std::exception& exception)
        {
            return WriteFailure("failed to serialize benchmark run evidence: " + std::string(exception.what()));
        }

        if (!output.good())
        {
            return WriteFailure("failed to flush benchmark JSONL artifact");
        }

        return {};
    }

    BenchmarkRunArtifactWriteResult BenchmarkServeArtifactWriter::Start(const BenchmarkRunArtifact& artifact)
    {
        if (started_ || artifact.runId.empty() || artifact.directory.empty())
        {
            return WriteFailure("serve artifact writer start context is invalid");
        }

        const std::filesystem::path benchmarkPath = artifact.directory / "benchmark.jsonl";
        output_.open(benchmarkPath, std::ios::binary | std::ios::trunc);
        if (!output_.is_open())
        {
            return WriteFailure("failed to open serve benchmark JSONL artifact");
        }

        try
        {
            const BenchmarkRunArtifactWriteResult metadataResult =
                WriteDocument(EncodeServeMetadata(artifact), &output_);
            if (!metadataResult.Succeeded())
            {
                return metadataResult;
            }
            output_.flush();
            if (!output_.good())
            {
                return WriteFailure("failed to flush serve benchmark metadata");
            }
        }
        catch (const std::exception& exception)
        {
            return WriteFailure("failed to serialize serve benchmark metadata: " + std::string(exception.what()));
        }

        runId_ = artifact.runId;
        started_ = true;
        return {};
    }

    BenchmarkRunArtifactWriteResult BenchmarkServeArtifactWriter::AppendPeriodicSample(
        const std::uint64_t sequence, const BenchmarkServerProcessSampleV1& processSample,
        const BenchmarkRuntimeSampleV1& runtimeSample)
    {
        if (!started_ || completed_ || sequence == 0 || processSample.sequence != sequence ||
            LifecycleStateToken(runtimeSample.lifecycleState).empty())
        {
            return WriteFailure("serve periodic sample is invalid");
        }

        try
        {
            const BenchmarkRunArtifactWriteResult sampleResult =
                WriteDocument(EncodeServePeriodicSample(runId_, sequence, processSample, runtimeSample), &output_);
            if (!sampleResult.Succeeded())
            {
                return sampleResult;
            }
            output_.flush();
            if (!output_.good())
            {
                return WriteFailure("failed to flush serve periodic sample");
            }
        }
        catch (const std::exception& exception)
        {
            return WriteFailure("failed to serialize serve periodic sample: " + std::string(exception.what()));
        }

        ++sampleCount_;
        return {};
    }

    BenchmarkRunArtifactWriteResult BenchmarkServeArtifactWriter::Complete()
    {
        if (!started_ || completed_)
        {
            return WriteFailure("serve artifact writer completion state is invalid");
        }

        try
        {
            const BenchmarkRunArtifactWriteResult summaryResult =
                WriteDocument(EncodeServeSummary(runId_, sampleCount_), &output_);
            if (!summaryResult.Succeeded())
            {
                return summaryResult;
            }
            output_.flush();
            if (!output_.good())
            {
                return WriteFailure("failed to flush serve terminal summary");
            }
        }
        catch (const std::exception& exception)
        {
            return WriteFailure("failed to serialize serve terminal summary: " + std::string(exception.what()));
        }

        completed_ = true;
        return {};
    }
} // namespace psnr::benchmark
