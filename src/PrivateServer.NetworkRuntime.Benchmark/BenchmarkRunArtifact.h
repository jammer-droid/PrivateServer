#pragma once

#include "BenchmarkClientWorkload.h"
#include "BenchmarkIpcEvent.h"
#include "BenchmarkServerProcessSampler.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    struct BenchmarkRunMetadataV1 final
    {
        std::string baselineSetId;
        std::string startedAtUtc;
        std::string configHash;
        std::string windowsVersion;
        std::string cpuModel;
        std::uint64_t totalPhysicalMemoryBytes = 0;
        std::uint32_t repeatIndex = 0;
        std::uint32_t repeatCount = 0;
        std::uint32_t logicalProcessorCount = 0;
    };

    struct BenchmarkRunArtifact final
    {
        std::string runId;
        std::filesystem::path directory;
        BenchmarkRunMetadataV1 metadata;
    };

    struct BenchmarkPeriodicSampleV1 final
    {
        std::uint64_t sequence = 0;
        BenchmarkWorkloadPhase phase = BenchmarkWorkloadPhase::Connect;
        BenchmarkServerProcessSampleV1 process;
        BenchmarkRuntimeSampleV1 runtime;
    };

    struct BenchmarkRunArtifactCreateResult final
    {
        BenchmarkRunArtifact artifact;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkRunArtifactFactory final
    {
    public:
        [[nodiscard]] static bool TryCreateBaselineSetId(std::string* outBaselineSetId);
        [[nodiscard]] static bool TryCreateRunId(std::string* outRunId);
        [[nodiscard]] static BenchmarkRunArtifactCreateResult Create(std::string_view outputRoot,
                                                                     std::string_view normalizedConfigJson,
                                                                     std::string_view baselineSetId,
                                                                     std::uint32_t repeatIndex,
                                                                     std::uint32_t repeatCount);
    };

    struct BenchmarkRunCompletionV1 final
    {
        bool valid = false;
        std::string error;
    };

    struct BenchmarkRunArtifactWriteResult final
    {
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkRunArtifactWriter final
    {
    public:
        [[nodiscard]] static BenchmarkRunArtifactWriteResult WriteRunEvidence(
            const BenchmarkRunArtifact& artifact, std::span<const BenchmarkPeriodicSampleV1> periodicSamples,
            const BenchmarkClientWorkloadResult& workloadResult, const BenchmarkRunCompletionV1& completion);
    };

    class BenchmarkServeArtifactWriter final
    {
    public:
        BenchmarkServeArtifactWriter() = default;

        BenchmarkServeArtifactWriter(const BenchmarkServeArtifactWriter&) = delete;
        BenchmarkServeArtifactWriter& operator=(const BenchmarkServeArtifactWriter&) = delete;

        BenchmarkServeArtifactWriter(BenchmarkServeArtifactWriter&&) = delete;
        BenchmarkServeArtifactWriter& operator=(BenchmarkServeArtifactWriter&&) = delete;

        [[nodiscard]] BenchmarkRunArtifactWriteResult Start(const BenchmarkRunArtifact& artifact);
        [[nodiscard]] BenchmarkRunArtifactWriteResult AppendPeriodicSample(
            std::uint64_t sequence, const BenchmarkServerProcessSampleV1& processSample,
            const BenchmarkRuntimeSampleV1& runtimeSample);
        [[nodiscard]] BenchmarkRunArtifactWriteResult Complete();

    private:
        std::ofstream output_;
        std::string runId_;
        std::uint64_t sampleCount_ = 0;
        bool started_ = false;
        bool completed_ = false;
    };
} // namespace psnr::benchmark
