#include "BenchmarkStatistics.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace psnr::benchmark
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T NearestRank(const std::vector<T>& sortedSamples, const std::size_t percentile)
        {
            if (sortedSamples.empty() || percentile == 0 || percentile > 100)
            {
                throw std::invalid_argument("percentile must be between 1 and 100 for non-empty samples");
            }
            const std::size_t rank = (percentile * sortedSamples.size() + 99) / 100;
            return sortedSamples[rank - 1];
        }

        [[nodiscard]] std::int64_t SignedDelta(const std::uint64_t start, const std::uint64_t end)
        {
            const std::uint64_t magnitude = end >= start ? end - start : start - end;
            if (magnitude > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            {
                throw std::overflow_error("process memory delta exceeds the signed artifact range");
            }
            return end >= start ? static_cast<std::int64_t>(magnitude) : -static_cast<std::int64_t>(magnitude);
        }
    } // namespace

    std::optional<BenchmarkLatencyStatisticsV1> BenchmarkStatistics::SummarizeLatency(
        const std::span<const std::int64_t> samples)
    {
        if (samples.empty())
        {
            return std::nullopt;
        }

        std::vector<std::int64_t> sortedSamples(samples.begin(), samples.end());
        std::sort(sortedSamples.begin(), sortedSamples.end());

        long double total = 0.0L;
        for (const std::int64_t sample : samples)
        {
            total += static_cast<long double>(sample);
        }

        BenchmarkLatencyStatisticsV1 statistics;
        statistics.sampleCount = sortedSamples.size();
        statistics.minimum = sortedSamples.front();
        statistics.mean = static_cast<double>(total / static_cast<long double>(sortedSamples.size()));
        statistics.p50 = NearestRank(sortedSamples, 50);
        statistics.p95 = NearestRank(sortedSamples, 95);
        statistics.p99 = NearestRank(sortedSamples, 99);
        statistics.maximum = sortedSamples.back();
        return statistics;
    }

    std::optional<BenchmarkProcessStatisticsV1> BenchmarkStatistics::SummarizeProcess(
        const std::span<const BenchmarkServerProcessSampleV1> samples)
    {
        if (samples.empty())
        {
            return std::nullopt;
        }

        const BenchmarkServerProcessSampleV1& first = samples.front();
        const BenchmarkServerProcessSampleV1& last = samples.back();

        BenchmarkProcessStatisticsV1 statistics;
        statistics.sampleCount = samples.size();
        statistics.firstSequence = first.sequence;
        statistics.lastSequence = last.sequence;
        statistics.cpu.logicalProcessorCount = first.logicalProcessorCount;
        statistics.memory.workingSetStartBytes = first.workingSetBytes;
        statistics.memory.workingSetEndBytes = last.workingSetBytes;
        statistics.memory.privateUsageStartBytes = first.privateUsageBytes;
        statistics.memory.privateUsageEndBytes = last.privateUsageBytes;
        statistics.memory.privateUsageDeltaBytes = SignedDelta(first.privateUsageBytes, last.privateUsageBytes);

        std::vector<double> intervalCpuPercents;
        for (const BenchmarkServerProcessSampleV1& sample : samples)
        {
            statistics.memory.workingSetPeakBytes =
                (std::max)(statistics.memory.workingSetPeakBytes, sample.workingSetBytes);
            statistics.memory.privateUsagePeakBytes =
                (std::max)(statistics.memory.privateUsagePeakBytes, sample.privateUsageBytes);
            if (sample.hasIntervalCpuPercent)
            {
                intervalCpuPercents.push_back(sample.intervalCpuPercent);
            }
        }

        std::sort(intervalCpuPercents.begin(), intervalCpuPercents.end());
        statistics.cpu.intervalSampleCount = intervalCpuPercents.size();
        if (!intervalCpuPercents.empty())
        {
            statistics.cpu.hasIntervalStatistics = true;
            statistics.cpu.intervalP95Percent = NearestRank(intervalCpuPercents, 95);
            statistics.cpu.intervalMaximumPercent = intervalCpuPercents.back();
        }

        if (samples.size() >= 2 && first.logicalProcessorCount != 0 &&
            first.logicalProcessorCount == last.logicalProcessorCount &&
            last.elapsedNanoseconds > first.elapsedNanoseconds &&
            last.kernelTime100Nanoseconds >= first.kernelTime100Nanoseconds &&
            last.userTime100Nanoseconds >= first.userTime100Nanoseconds)
        {
            const std::uint64_t cpuDelta100Nanoseconds =
                (last.kernelTime100Nanoseconds - first.kernelTime100Nanoseconds) +
                (last.userTime100Nanoseconds - first.userTime100Nanoseconds);
            const std::uint64_t elapsedNanoseconds = last.elapsedNanoseconds - first.elapsedNanoseconds;
            const long double normalizedPercent =
                static_cast<long double>(cpuDelta100Nanoseconds) * 10'000.0L /
                (static_cast<long double>(elapsedNanoseconds) * static_cast<long double>(first.logicalProcessorCount));
            statistics.cpu.hasOverallAveragePercent = true;
            statistics.cpu.overallAveragePercent = static_cast<double>(normalizedPercent);
            statistics.cpu.observedIntervalNanoseconds = elapsedNanoseconds;
        }

        return statistics;
    }
} // namespace psnr::benchmark
