#include "BenchmarkWorldMergedArtifact.h"

#include "BenchmarkStatistics.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
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
        constexpr std::string_view MergedSchema = "psnr.benchmark.world_host.merged";
        constexpr std::uint64_t MergedVersion = 1;
        constexpr std::uint64_t SchemaVersion = 1;
        constexpr std::uint64_t WorldHostConfigSchemaVersion = 2;
        constexpr std::uint64_t WorldReportSchemaVersion = 2;
        constexpr std::uint64_t P99LimitNanoseconds = 16'670'000;
        constexpr double OverrunRatioLimit = 0.01;
        constexpr std::uint64_t CanonicalTickRateHz = 60;
        constexpr std::uint64_t CanonicalRoundDurationTicks = 10'800;
        constexpr std::uint64_t CanonicalClientCount = 100;

        using JsonObject = nlohmann::ordered_json;

        struct LoadedArtifact final
        {
            JsonObject value;
            bool loaded = false;
            bool identityValid = false;
        };

        struct PhaseDefinition final
        {
            std::string_view name;
            std::uint64_t startSecond = 0;
            std::uint64_t endSecond = 0;
        };

        constexpr PhaseDefinition PhaseDefinitions[] = {
            {"early", 10, 60},
            {"mid", 60, 120},
            {"late", 120, 180},
        };

        void AddError(JsonObject* const errors, const std::string& message)
        {
            errors->push_back(message);
        }

        [[nodiscard]] bool HasSchema(const JsonObject& document, const std::string_view expectedSchema,
                                     const std::uint64_t expectedVersion)
        {
            const JsonObject::const_iterator schema = document.find("schema");
            const JsonObject::const_iterator version = document.find("version");
            return schema != document.end() && schema->is_string() && schema->get<std::string>() == expectedSchema &&
                   version != document.end() && version->is_number_unsigned() &&
                   version->get<std::uint64_t>() == expectedVersion;
        }

        [[nodiscard]] bool HasRunId(const JsonObject& document, const std::string_view runId)
        {
            const JsonObject::const_iterator value = document.find("runId");
            return value != document.end() && value->is_string() && value->get<std::string>() == runId;
        }

        [[nodiscard]] LoadedArtifact LoadDocument(const std::filesystem::path& path,
                                                  const std::string_view relativePath,
                                                  const std::string_view expectedSchema,
                                                  const std::uint64_t expectedVersion, const std::string_view runId,
                                                  JsonObject* const errors, const bool requireRunId = true)
        {
            LoadedArtifact artifact;
            try
            {
                std::ifstream input{path, std::ios::binary};
                if (!input.is_open())
                {
                    AddError(errors, std::string{"missing artifact: "} + std::string{relativePath});
                    return artifact;
                }
                artifact.value = JsonObject::parse(input, nullptr, false);
                if (artifact.value.is_discarded() || !artifact.value.is_object())
                {
                    AddError(errors, std::string{"invalid JSON artifact: "} + std::string{relativePath});
                    return artifact;
                }
                artifact.loaded = true;
                if (!HasSchema(artifact.value, expectedSchema, expectedVersion))
                {
                    AddError(errors, std::string{"schema mismatch: "} + std::string{relativePath});
                }
                artifact.identityValid = !requireRunId || HasRunId(artifact.value, runId);
                if (!artifact.identityValid)
                {
                    AddError(errors, std::string{"runId mismatch: "} + std::string{relativePath});
                }
            }
            catch (const std::exception& exception)
            {
                AddError(errors, std::string{"failed to read "} + std::string{relativePath} + ": " + exception.what());
            }
            return artifact;
        }

        [[nodiscard]] LoadedArtifact LoadJsonLines(const std::filesystem::path& path,
                                                   const std::string_view relativePath,
                                                   const std::string_view expectedSchema, const std::string_view runId,
                                                   JsonObject* const errors)
        {
            LoadedArtifact artifact;
            artifact.value = JsonObject::array();
            try
            {
                std::ifstream input{path, std::ios::binary};
                if (!input.is_open())
                {
                    AddError(errors, std::string{"missing artifact: "} + std::string{relativePath});
                    return artifact;
                }

                bool identityValid = true;
                std::string line;
                std::size_t lineNumber = 0;
                while (std::getline(input, line))
                {
                    ++lineNumber;
                    if (line.empty())
                    {
                        continue;
                    }
                    JsonObject document = JsonObject::parse(line, nullptr, false);
                    if (document.is_discarded() || !document.is_object())
                    {
                        AddError(errors, std::string{"invalid JSONL record: "} + std::string{relativePath} + ":" +
                                             std::to_string(lineNumber));
                        continue;
                    }
                    if (!HasSchema(document, expectedSchema, SchemaVersion))
                    {
                        AddError(errors, std::string{"schema mismatch: "} + std::string{relativePath} + ":" +
                                             std::to_string(lineNumber));
                    }
                    if (!HasRunId(document, runId))
                    {
                        identityValid = false;
                        AddError(errors, std::string{"runId mismatch: "} + std::string{relativePath} + ":" +
                                             std::to_string(lineNumber));
                    }
                    artifact.value.push_back(std::move(document));
                }
                if (!input.eof() || artifact.value.empty())
                {
                    AddError(errors, std::string{"empty or unreadable JSONL artifact: "} + std::string{relativePath});
                    return artifact;
                }
                artifact.loaded = true;
                artifact.identityValid = identityValid;
            }
            catch (const std::exception& exception)
            {
                AddError(errors, std::string{"failed to read "} + std::string{relativePath} + ": " + exception.what());
            }
            return artifact;
        }

        [[nodiscard]] bool IsCompleteReport(const LoadedArtifact& report)
        {
            if (!report.loaded)
            {
                return false;
            }
            const JsonObject::const_iterator status = report.value.find("status");
            return status != report.value.end() && status->is_string() && status->get<std::string>() == "complete";
        }

        [[nodiscard]] std::optional<std::uint64_t> ReadUnsigned(const JsonObject& document, const std::string_view key)
        {
            const JsonObject::const_iterator value = document.find(key);
            if (value == document.end() || !value->is_number_unsigned())
            {
                return std::nullopt;
            }
            return value->get<std::uint64_t>();
        }

        [[nodiscard]] bool ReadNestedUnsigned(const JsonObject& document, const std::string_view firstKey,
                                              const std::string_view secondKey, std::uint64_t* const outValue)
        {
            const JsonObject::const_iterator first = document.find(firstKey);
            if (first == document.end() || !first->is_object())
            {
                return false;
            }
            const std::optional<std::uint64_t> value = ReadUnsigned(*first, secondKey);
            if (!value.has_value())
            {
                return false;
            }
            *outValue = *value;
            return true;
        }

        [[nodiscard]] bool ReadNestedUnsigned(const JsonObject& document, const std::string_view firstKey,
                                              const std::string_view secondKey, const std::string_view thirdKey,
                                              std::uint64_t* const outValue)
        {
            const JsonObject::const_iterator first = document.find(firstKey);
            if (first == document.end() || !first->is_object())
            {
                return false;
            }
            return ReadNestedUnsigned(*first, secondKey, thirdKey, outValue);
        }

        [[nodiscard]] JsonObject BuildPhaseSummary(const JsonObject& tickSamples, const PhaseDefinition& definition,
                                                   const std::uint64_t tickRateHz,
                                                   const std::uint64_t observedRoundStartTick, JsonObject* const errors)
        {
            std::vector<std::int64_t> executionDurations;
            std::vector<std::int64_t> startLags;
            std::uint64_t overrunCount = 0;
            const std::uint64_t startOffset = definition.startSecond * tickRateHz;
            const std::uint64_t endOffset = definition.endSecond * tickRateHz;
            const std::uint64_t tickBudgetNanoseconds = 1'000'000'000ULL / tickRateHz;

            for (const JsonObject& sample : tickSamples)
            {
                const std::optional<std::uint64_t> firstServerTick = ReadUnsigned(sample, "firstServerTick");
                const std::optional<std::uint64_t> executionDuration =
                    ReadUnsigned(sample, "executionDurationNanoseconds");
                const std::optional<std::uint64_t> startLag = ReadUnsigned(sample, "startLagNanoseconds");
                if (!firstServerTick.has_value() || !executionDuration.has_value() || !startLag.has_value() ||
                    *firstServerTick < observedRoundStartTick ||
                    *executionDuration > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
                    *startLag > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                {
                    AddError(errors, "invalid tick sample numeric field");
                    continue;
                }

                const std::uint64_t offset = *firstServerTick - observedRoundStartTick;
                if (offset < startOffset || offset >= endOffset)
                {
                    continue;
                }
                executionDurations.push_back(static_cast<std::int64_t>(*executionDuration));
                startLags.push_back(static_cast<std::int64_t>(*startLag));
                if (*executionDuration > tickBudgetNanoseconds)
                {
                    ++overrunCount;
                }
            }

            JsonObject summary = JsonObject::object();
            summary["startSecond"] = definition.startSecond;
            summary["endSecond"] = definition.endSecond;
            summary["sampleCount"] = executionDurations.size();
            summary["overrunCount"] = overrunCount;
            const double overrunRatio = executionDurations.empty() ? 0.0
                                                                   : static_cast<double>(overrunCount) /
                                                                         static_cast<double>(executionDurations.size());
            summary["executionOverrunRatio"] = overrunRatio;

            const std::optional<BenchmarkLatencyStatisticsV1> execution =
                BenchmarkStatistics::SummarizeLatency(executionDurations);
            const std::optional<BenchmarkLatencyStatisticsV1> lag = BenchmarkStatistics::SummarizeLatency(startLags);
            if (execution.has_value() && lag.has_value())
            {
                summary["executionDurationNanoseconds"] = {
                    {"algorithm", BenchmarkStatistics::PercentileAlgorithm},
                    {"min", execution->minimum},
                    {"mean", execution->mean},
                    {"p50", execution->p50},
                    {"p95", execution->p95},
                    {"p99", execution->p99},
                    {"max", execution->maximum},
                };
                summary["startLagNanoseconds"] = {
                    {"algorithm", BenchmarkStatistics::PercentileAlgorithm},
                    {"min", lag->minimum},
                    {"mean", lag->mean},
                    {"p50", lag->p50},
                    {"p95", lag->p95},
                    {"p99", lag->p99},
                    {"max", lag->maximum},
                };
                const bool p99Passed = execution->p99 < static_cast<std::int64_t>(P99LimitNanoseconds);
                const bool overrunRatioPassed = overrunRatio < OverrunRatioLimit;
                summary["p99Passed"] = p99Passed;
                summary["overrunRatioPassed"] = overrunRatioPassed;
                summary["passed"] = p99Passed && overrunRatioPassed;
            }
            else
            {
                summary["p99Passed"] = false;
                summary["overrunRatioPassed"] = false;
                summary["passed"] = false;
                AddError(errors, std::string{"missing phase samples: "} + std::string{definition.name});
            }
            return summary;
        }

        [[nodiscard]] bool AllRuntimeSamplesSucceeded(const LoadedArtifact& runtimeSamples)
        {
            if (!runtimeSamples.loaded)
            {
                return false;
            }
            for (const JsonObject& sample : runtimeSamples.value)
            {
                const JsonObject::const_iterator succeeded = sample.find("snapshotCaptureSucceeded");
                if (succeeded == sample.end() || !succeeded->is_boolean() || !succeeded->get<bool>())
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    BenchmarkWorldMergedArtifactWriteResult BenchmarkWorldMergedArtifact::Write(const std::filesystem::path& runsRoot,
                                                                                const std::string_view runId)
    {
        BenchmarkWorldMergedArtifactWriteResult result;
        if (runsRoot.empty() || runId.empty())
        {
            result.error = "World merged artifact identity is empty";
            return result;
        }

        try
        {
            const std::filesystem::path runDirectory = runsRoot / runId;
            JsonObject errors = JsonObject::array();
            const LoadedArtifact manifest = LoadDocument(runDirectory / "run-manifest.json", "run-manifest.json",
                                                         "ps.run.manifest", SchemaVersion, runId, &errors);
            const LoadedArtifact worldConfig =
                LoadDocument(runDirectory / "world" / "effective-config.json", "world/effective-config.json",
                             "psnr.world_server.host.config", WorldHostConfigSchemaVersion, runId, &errors, false);
            const LoadedArtifact worldReport = LoadDocument(runDirectory / "world" / "report.json", "world/report.json",
                                                            "psnr.world.report", WorldReportSchemaVersion, runId, &errors);
            const LoadedArtifact tickSamples =
                LoadJsonLines(runDirectory / "world" / "tick-samples.jsonl", "world/tick-samples.jsonl",
                              "psnr.world.tick_sample", runId, &errors);
            const LoadedArtifact runtimeReport = LoadDocument(
                runDirectory / "runtime" / "report.json", "runtime/report.json", "psnr.runtime.report", SchemaVersion,
                runId, &errors);
            const LoadedArtifact runtimeSamples =
                LoadJsonLines(runDirectory / "runtime" / "samples.jsonl", "runtime/samples.jsonl",
                              "psnr.runtime.sample", runId, &errors);
            const LoadedArtifact benchmarkConfig =
                LoadDocument(runDirectory / "benchmark" / "effective-config.json", "benchmark/effective-config.json",
                             "psnr.benchmark.world_host.effective_config", SchemaVersion, runId, &errors);
            const LoadedArtifact processSummary =
                LoadDocument(runDirectory / "benchmark" / "process-summary.json", "benchmark/process-summary.json",
                             "psnr.benchmark.world_host.process_summary", SchemaVersion, runId, &errors);
            const LoadedArtifact processSamples =
                LoadJsonLines(runDirectory / "benchmark" / "process-samples.jsonl", "benchmark/process-samples.jsonl",
                              "psnr.benchmark.world_host.process_sample", runId, &errors);
            const LoadedArtifact clients =
                LoadJsonLines(runDirectory / "benchmark" / "clients.jsonl", "benchmark/clients.jsonl",
                              "psnr.benchmark.world_host.client_result", runId, &errors);

            std::uint64_t tickRateHz = 0;
            if (!worldConfig.loaded || !ReadNestedUnsigned(worldConfig.value, "execution", "tickRateHz", &tickRateHz) ||
                tickRateHz == 0)
            {
                AddError(&errors, "world execution.tickRateHz is invalid");
            }
            std::uint64_t roundDurationTicks = 0;
            std::uint64_t configuredClientCount = 0;
            const bool canonicalProfile =
                worldConfig.loaded && tickRateHz == CanonicalTickRateHz &&
                ReadNestedUnsigned(worldConfig.value, "gameplay", "roundDurationTicks", &roundDurationTicks) &&
                roundDurationTicks == CanonicalRoundDurationTicks && benchmarkConfig.loaded &&
                ReadNestedUnsigned(benchmarkConfig.value, "controllerConfig", "clients", "count",
                                   &configuredClientCount) &&
                configuredClientCount == CanonicalClientCount;
            if (!canonicalProfile)
            {
                AddError(&errors,
                         "canonical World benchmark profile does not match 60 Hz, 10800 ticks, and 100 clients");
            }

            std::uint64_t observedRoundStartTick = 0;
            if (tickSamples.loaded)
            {
                const std::optional<std::uint64_t> firstTick =
                    ReadUnsigned(tickSamples.value.front(), "firstServerTick");
                if (firstTick.has_value())
                {
                    observedRoundStartTick = *firstTick;
                }
            }
            if (observedRoundStartTick == 0)
            {
                AddError(&errors, "observed World round start tick is unavailable");
            }

            JsonObject phases = JsonObject::object();
            bool phasesPassed = tickRateHz != 0 && observedRoundStartTick != 0;
            if (phasesPassed)
            {
                for (const PhaseDefinition& definition : PhaseDefinitions)
                {
                    JsonObject phase =
                        BuildPhaseSummary(tickSamples.value, definition, tickRateHz, observedRoundStartTick, &errors);
                    const JsonObject::const_iterator passed = phase.find("passed");
                    phasesPassed = phasesPassed && passed != phase.end() && passed->is_boolean() && passed->get<bool>();
                    phases[definition.name] = std::move(phase);
                }
            }

            bool worldSampleCountMatches = false;
            if (worldReport.loaded && tickSamples.loaded)
            {
                const std::optional<std::uint64_t> writtenSampleCount =
                    ReadUnsigned(worldReport.value, "writtenSampleCount");
                worldSampleCountMatches =
                    writtenSampleCount.has_value() && *writtenSampleCount == tickSamples.value.size();
            }
            if (!worldSampleCountMatches)
            {
                AddError(&errors, "World report sample count does not match raw tick samples");
            }

            bool processSampleCountMatches = false;
            if (processSummary.loaded && processSamples.loaded)
            {
                const std::optional<std::uint64_t> summarySampleCount =
                    ReadUnsigned(processSummary.value, "sampleCount");
                std::uint64_t measurementSampleCount = 0;
                for (const JsonObject& sample : processSamples.value)
                {
                    const JsonObject::const_iterator phase = sample.find("phase");
                    if (phase != sample.end() && phase->is_string() && phase->get<std::string>() == "measurement")
                    {
                        ++measurementSampleCount;
                    }
                }
                processSampleCountMatches =
                    summarySampleCount.has_value() && *summarySampleCount == measurementSampleCount;
            }
            if (!processSampleCountMatches)
            {
                AddError(&errors, "process summary sample count does not match measurement samples");
            }

            bool clientGatePassed = false;
            if (clients.loaded && clients.value.size() == 1)
            {
                const JsonObject& client = clients.value.front();
                const std::optional<std::uint64_t> requested = ReadUnsigned(client, "requestedClientCount");
                const std::optional<std::uint64_t> joined = ReadUnsigned(client, "joinedClientCount");
                const std::optional<std::uint64_t> completed = ReadUnsigned(client, "completedClientCount");
                const std::optional<std::uint64_t> errorCount = ReadUnsigned(client, "errorCount");
                const JsonObject::const_iterator clientErrors = client.find("errors");
                clientGatePassed = requested.has_value() && joined.has_value() && completed.has_value() &&
                                   errorCount.has_value() && *requested == *joined && *joined == *completed &&
                                   *requested == configuredClientCount && *errorCount == 0 &&
                                   clientErrors != client.end() && clientErrors->is_array() && clientErrors->empty();
            }
            if (!clientGatePassed)
            {
                AddError(&errors, "client correctness gate failed");
            }

            const bool identityValid = manifest.identityValid && worldConfig.identityValid &&
                                       worldReport.identityValid && tickSamples.identityValid &&
                                       runtimeReport.identityValid && runtimeSamples.identityValid &&
                                       benchmarkConfig.identityValid && processSummary.identityValid &&
                                       processSamples.identityValid && clients.identityValid;
            const bool worldComplete = IsCompleteReport(worldReport) && worldSampleCountMatches;
            const bool runtimeComplete = IsCompleteReport(runtimeReport) && AllRuntimeSamplesSucceeded(runtimeSamples);
            const bool processComplete = processSummary.loaded && processSampleCountMatches;
            const bool artifactsComplete = manifest.loaded && worldConfig.loaded && worldComplete && runtimeComplete &&
                                           benchmarkConfig.loaded && processComplete && clients.loaded;
            if (!worldComplete)
            {
                AddError(&errors, "World artifact completeness gate failed");
            }
            if (!runtimeComplete)
            {
                AddError(&errors, "Runtime artifact completeness gate failed");
            }
            if (!processComplete)
            {
                AddError(&errors, "process artifact completeness gate failed");
            }
            const bool verdictValid = identityValid && artifactsComplete && canonicalProfile && phasesPassed &&
                                      clientGatePassed && errors.empty();

            JsonObject identity = JsonObject::object();
            identity["consistent"] = identityValid;
            identity["runId"] = runId;

            JsonObject completeness = JsonObject::object();
            completeness["complete"] = artifactsComplete;
            completeness["world"] = worldComplete;
            completeness["runtime"] = runtimeComplete;
            completeness["process"] = processComplete;
            completeness["clients"] = clients.loaded;

            JsonObject gates = JsonObject::object();
            gates["phasePerformance"] = phasesPassed;
            gates["clientCorrectness"] = clientGatePassed;
            gates["canonicalProfile"] = canonicalProfile;
            gates["p99LimitNanosecondsExclusive"] = P99LimitNanoseconds;
            gates["executionOverrunRatioLimitExclusive"] = OverrunRatioLimit;

            JsonObject verdict = JsonObject::object();
            verdict["valid"] = verdictValid;
            verdict["outcome"] = verdictValid ? "valid" : "invalid";
            verdict["gates"] = std::move(gates);
            verdict["errors"] = errors;

            JsonObject sources = JsonObject::object();
            sources["runManifest"] = manifest.value;
            sources["worldEffectiveConfig"] = worldConfig.value;
            sources["worldReport"] = worldReport.value;
            sources["worldTickSamples"] = tickSamples.value;
            sources["runtimeReport"] = runtimeReport.value;
            sources["runtimeSamples"] = runtimeSamples.value;
            sources["benchmarkEffectiveConfig"] = benchmarkConfig.value;
            sources["processSummary"] = processSummary.value;
            sources["processSamples"] = processSamples.value;
            sources["clients"] = clients.value;

            JsonObject phaseDefinition = JsonObject::object();
            phaseDefinition["origin"] = "first observed running sample firstServerTick";
            phaseDefinition["observedRoundStartTick"] = observedRoundStartTick;
            phaseDefinition["tickRateHz"] = tickRateHz;

            JsonObject document = JsonObject::object();
            document["schema"] = MergedSchema;
            document["version"] = MergedVersion;
            document["runId"] = runId;
            document["identity"] = std::move(identity);
            document["completeness"] = std::move(completeness);
            document["phaseDefinition"] = std::move(phaseDefinition);
            document["phases"] = std::move(phases);
            document["sources"] = std::move(sources);
            document["verdict"] = std::move(verdict);

            const std::filesystem::path path = runDirectory / "benchmark" / "merged.json";
            std::ofstream output{path, std::ios::binary | std::ios::trunc};
            if (!output.is_open())
            {
                result.error = "failed to open World merged artifact";
                return result;
            }
            output << document.dump(2) << '\n';
            output.flush();
            if (!output.good())
            {
                result.error = "failed to write World merged artifact";
                return result;
            }
            result.valid = verdictValid;
        }
        catch (const std::exception& exception)
        {
            result.error = std::string{"failed to create World merged artifact: "} + exception.what();
        }
        catch (...)
        {
            result.error = "failed to create World merged artifact with an unknown error";
        }
        return result;
    }
} // namespace psnr::benchmark
