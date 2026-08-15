#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    struct BenchmarkProfileConfigV1 final
    {
        std::string id = "steady-roundtrip";
        std::uint32_t version = 1;
        std::string purpose = "fixed public-path roundtrip baseline";
    };

    struct BenchmarkServerConfigV1 final
    {
        std::string address = "127.0.0.1";
        std::uint16_t port = 42057;
    };

    struct BenchmarkConnectionConfigV1 final
    {
        std::uint32_t clientCount = 128;
        std::uint32_t batchSize = 16;
        std::uint32_t timeoutSeconds = 30;
    };

    struct BenchmarkWorkloadConfigV1 final
    {
        std::string operation = "echo";
        std::uint32_t requestRatePerClient = 60;
        std::uint32_t workerCount = 4;
        std::uint32_t semanticPayloadBytes = 64;
    };

    struct BenchmarkPhaseConfigV1 final
    {
        std::uint32_t warmupSeconds = 10;
        std::uint32_t measurementSeconds = 60;
        std::uint32_t drainTimeoutSeconds = 10;
        std::uint32_t shutdownTimeoutSeconds = 10;
    };

    struct BenchmarkSamplingConfigV1 final
    {
        std::uint32_t intervalMs = 1000;
    };

    struct BenchmarkExecutionConfigV1 final
    {
        std::uint32_t repeats = 5;
    };

    struct BenchmarkArtifactConfigV1 final
    {
        std::string format = "jsonl";
        std::string outputRoot = "artifacts/network-runtime-benchmark";
    };

    struct BenchmarkConfigV1 final
    {
        BenchmarkProfileConfigV1 profile;
        BenchmarkServerConfigV1 server;
        BenchmarkConnectionConfigV1 connection;
        BenchmarkWorkloadConfigV1 workload;
        BenchmarkPhaseConfigV1 phases;
        BenchmarkSamplingConfigV1 sampling;
        BenchmarkExecutionConfigV1 execution;
        BenchmarkArtifactConfigV1 artifact;
    };

    struct BenchmarkConfigParseResult final
    {
        BenchmarkConfigV1 config;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkConfigV1Codec final
    {
    public:
        [[nodiscard]] static BenchmarkConfigV1 Canonical();
        [[nodiscard]] static BenchmarkConfigParseResult Parse(std::string_view jsonText);
        [[nodiscard]] static std::string Validate(const BenchmarkConfigV1& config);
        [[nodiscard]] static std::string SerializeNormalized(const BenchmarkConfigV1& config);
    };
} // namespace psnr::benchmark
