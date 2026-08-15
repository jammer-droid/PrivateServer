#pragma once

#include "BenchmarkServerProcessSampler.h"
#include "BenchmarkStatistics.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace psnr::benchmark
{
    enum class BenchmarkWorldMeasurementPhase : std::uint8_t
    {
        Admission,
        Measurement,
        Drain,
    };

    struct BenchmarkWorldProcessSampleV1 final
    {
        BenchmarkWorldMeasurementPhase phase = BenchmarkWorldMeasurementPhase::Admission;
        BenchmarkServerProcessSampleV1 process;
    };

    struct BenchmarkWorldArtifactSummaryWriteResult final
    {
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkWorldArtifactSummary final
    {
    public:
        [[nodiscard]] static std::string_view PhaseName(BenchmarkWorldMeasurementPhase phase) noexcept;
        [[nodiscard]] static std::vector<BenchmarkServerProcessSampleV1> FilterMeasurementProcessSamples(
            std::span<const BenchmarkWorldProcessSampleV1> samples);
        [[nodiscard]] static std::optional<BenchmarkProcessStatisticsV1> SummarizeMeasurementProcess(
            std::span<const BenchmarkWorldProcessSampleV1> samples);
        [[nodiscard]] static BenchmarkWorldArtifactSummaryWriteResult WriteMeasurementProcessSummary(
            const std::filesystem::path& runsRoot, std::string_view runId,
            std::span<const BenchmarkWorldProcessSampleV1> samples);
    };
} // namespace psnr::benchmark
