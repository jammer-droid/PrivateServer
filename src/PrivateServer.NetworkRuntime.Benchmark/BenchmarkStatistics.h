#pragma once

#include "BenchmarkServerProcessSampler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace psnr::benchmark
{
    struct BenchmarkLatencyStatisticsV1 final
    {
        std::size_t sampleCount = 0;
        std::int64_t minimum = 0;
        double mean = 0.0;
        std::int64_t p50 = 0;
        std::int64_t p95 = 0;
        std::int64_t p99 = 0;
        std::int64_t maximum = 0;
    };

    struct BenchmarkProcessCpuStatisticsV1 final
    {
        std::uint32_t logicalProcessorCount = 0;
        std::size_t intervalSampleCount = 0;
        double intervalP95Percent = 0.0;
        double intervalMaximumPercent = 0.0;
        double overallAveragePercent = 0.0;
        std::uint64_t observedIntervalNanoseconds = 0;
        bool hasIntervalStatistics = false;
        bool hasOverallAveragePercent = false;
    };

    struct BenchmarkProcessMemoryStatisticsV1 final
    {
        std::uint64_t workingSetStartBytes = 0;
        std::uint64_t workingSetEndBytes = 0;
        std::uint64_t workingSetPeakBytes = 0;
        std::uint64_t privateUsageStartBytes = 0;
        std::uint64_t privateUsageEndBytes = 0;
        std::uint64_t privateUsagePeakBytes = 0;
        std::int64_t privateUsageDeltaBytes = 0;
    };

    struct BenchmarkProcessStatisticsV1 final
    {
        std::size_t sampleCount = 0;
        std::uint64_t firstSequence = 0;
        std::uint64_t lastSequence = 0;
        BenchmarkProcessCpuStatisticsV1 cpu;
        BenchmarkProcessMemoryStatisticsV1 memory;
    };

    class BenchmarkStatistics final
    {
    public:
        static constexpr std::string_view PercentileAlgorithm = "nearest-rank-ceil-pn-minus-one";

        [[nodiscard]] static std::optional<BenchmarkLatencyStatisticsV1> SummarizeLatency(
            std::span<const std::int64_t> samples);
        [[nodiscard]] static std::optional<BenchmarkProcessStatisticsV1> SummarizeProcess(
            std::span<const BenchmarkServerProcessSampleV1> samples);
    };
} // namespace psnr::benchmark
