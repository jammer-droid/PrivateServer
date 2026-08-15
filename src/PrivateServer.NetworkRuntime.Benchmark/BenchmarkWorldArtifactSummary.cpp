#include "BenchmarkWorldArtifactSummary.h"

#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::string_view ProcessSummarySchema = "psnr.benchmark.world_host.process_summary";
        constexpr std::uint64_t ProcessSummaryVersion = 1;

        using JsonObject = nlohmann::ordered_json;
    } // namespace

    std::string_view BenchmarkWorldArtifactSummary::PhaseName(const BenchmarkWorldMeasurementPhase phase) noexcept
    {
        switch (phase)
        {
        case BenchmarkWorldMeasurementPhase::Admission:
            return "admission";
        case BenchmarkWorldMeasurementPhase::Measurement:
            return "measurement";
        case BenchmarkWorldMeasurementPhase::Drain:
            return "drain";
        default:
            return "unknown";
        }
    }

    std::vector<BenchmarkServerProcessSampleV1> BenchmarkWorldArtifactSummary::FilterMeasurementProcessSamples(
        const std::span<const BenchmarkWorldProcessSampleV1> samples)
    {
        std::vector<BenchmarkServerProcessSampleV1> filtered;
        filtered.reserve(samples.size());
        for (const BenchmarkWorldProcessSampleV1& sample : samples)
        {
            if (sample.phase == BenchmarkWorldMeasurementPhase::Measurement)
            {
                filtered.push_back(sample.process);
            }
        }
        return filtered;
    }

    std::optional<BenchmarkProcessStatisticsV1> BenchmarkWorldArtifactSummary::SummarizeMeasurementProcess(
        const std::span<const BenchmarkWorldProcessSampleV1> samples)
    {
        const std::vector<BenchmarkServerProcessSampleV1> filtered = FilterMeasurementProcessSamples(samples);
        return BenchmarkStatistics::SummarizeProcess(filtered);
    }

    BenchmarkWorldArtifactSummaryWriteResult BenchmarkWorldArtifactSummary::WriteMeasurementProcessSummary(
        const std::filesystem::path& runsRoot, const std::string_view runId,
        const std::span<const BenchmarkWorldProcessSampleV1> samples)
    {
        BenchmarkWorldArtifactSummaryWriteResult result;
        try
        {
            if (runId.empty())
            {
                result.error = "World Host process summary runId is empty";
                return result;
            }

            const std::optional<BenchmarkProcessStatisticsV1> statistics = SummarizeMeasurementProcess(samples);
            if (!statistics.has_value())
            {
                result.error = "World Host measurement process samples are empty";
                return result;
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
            document["schema"] = ProcessSummarySchema;
            document["version"] = ProcessSummaryVersion;
            document["runId"] = runId;
            document["phase"] = PhaseName(BenchmarkWorldMeasurementPhase::Measurement);
            document["sampleCount"] = statistics->sampleCount;
            document["firstSequence"] = statistics->firstSequence;
            document["lastSequence"] = statistics->lastSequence;
            document["cpu"] = std::move(cpu);
            document["memory"] = std::move(memory);

            const std::filesystem::path path = runsRoot / runId / "benchmark" / "process-summary.json";
            std::ofstream output{path, std::ios::binary | std::ios::trunc};
            if (!output.is_open())
            {
                result.error = "failed to open World Host process summary artifact";
                return result;
            }
            output << document.dump(2) << '\n';
            output.flush();
            if (!output.good())
            {
                result.error = "failed to write World Host process summary artifact";
            }
        }
        catch (const std::exception& exception)
        {
            result.error = std::string{"failed to create World Host process summary: "} + exception.what();
        }
        catch (...)
        {
            result.error = "failed to create World Host process summary with an unknown error";
        }
        return result;
    }
} // namespace psnr::benchmark
