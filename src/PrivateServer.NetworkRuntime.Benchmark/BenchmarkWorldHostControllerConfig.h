#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    struct BenchmarkWorldHostControllerConfigField final
    {
        std::string_view key;
        std::string_view path;
    };

    class BenchmarkWorldHostControllerConfigContract final
    {
    public:
        static constexpr std::string_view SchemaName = "psnr.world_server.benchmark.controller.config";
        static constexpr std::uint64_t SchemaVersion = 1;
        static constexpr std::uintmax_t MaximumDocumentBytes = 1024 * 1024;

        static constexpr BenchmarkWorldHostControllerConfigField Schema{"schema", "schema"};
        static constexpr BenchmarkWorldHostControllerConfigField Version{"version", "version"};

        static constexpr std::string_view WorldHostSection = "worldHost";
        static constexpr BenchmarkWorldHostControllerConfigField ExecutablePath{"executablePath",
                                                                                "worldHost.executablePath"};
        static constexpr BenchmarkWorldHostControllerConfigField ServerConfigPath{"configPath", "worldHost.configPath"};

        static constexpr std::string_view ArtifactSection = "artifact";
        static constexpr BenchmarkWorldHostControllerConfigField RunsRoot{"runsRoot", "artifact.runsRoot"};

        static constexpr std::string_view ClientsSection = "clients";
        static constexpr BenchmarkWorldHostControllerConfigField ClientAddress{"address", "clients.address"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientPort{"port", "clients.port"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientCount{"count", "clients.count"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientRampPerSecond{"rampPerSecond",
                                                                                     "clients.rampPerSecond"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientAdmissionTimeoutMilliseconds{
            "admissionTimeoutMilliseconds", "clients.admissionTimeoutMilliseconds"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientRoundResultTimeoutMilliseconds{
            "roundResultTimeoutMilliseconds", "clients.roundResultTimeoutMilliseconds"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientEventQueueCapacity{"eventQueueCapacity",
                                                                                          "clients.eventQueueCapacity"};
        static constexpr BenchmarkWorldHostControllerConfigField ClientPayloadQueueCapacity{
            "payloadQueueCapacity", "clients.payloadQueueCapacity"};

        static constexpr std::string_view WorkloadSection = "workload";
        static constexpr BenchmarkWorldHostControllerConfigField WorkloadSeed{"seed", "workload.seed"};
        static constexpr BenchmarkWorldHostControllerConfigField ControlIntervalMilliseconds{
            "controlIntervalMilliseconds", "workload.controlIntervalMilliseconds"};
        static constexpr BenchmarkWorldHostControllerConfigField BoostPercent{"boostPercent", "workload.boostPercent"};

        static constexpr std::string_view SamplingSection = "sampling";
        static constexpr BenchmarkWorldHostControllerConfigField SamplingIntervalMilliseconds{
            "intervalMilliseconds", "sampling.intervalMilliseconds"};

        static constexpr std::string_view LifecycleSection = "lifecycle";
        static constexpr BenchmarkWorldHostControllerConfigField ShutdownTimeoutMilliseconds{
            "shutdownTimeoutMilliseconds", "lifecycle.shutdownTimeoutMilliseconds"};

        static constexpr std::uint16_t DefaultClientPort = 27'015;
        static constexpr std::uint32_t DefaultClientCount = 100;
        static constexpr std::uint32_t DefaultClientRampPerSecond = 20;
        static constexpr std::uint32_t DefaultClientAdmissionTimeoutMilliseconds = 10'000;
        static constexpr std::uint32_t DefaultClientRoundResultTimeoutMilliseconds = 200'000;
        static constexpr std::uint32_t DefaultClientEventQueueCapacity = 4'096;
        static constexpr std::uint32_t DefaultClientPayloadQueueCapacity = 1'024;
        static constexpr std::uint32_t DefaultWorkloadSeed = 77;
        static constexpr std::uint32_t DefaultControlIntervalMilliseconds = 500;
        static constexpr std::uint32_t DefaultBoostPercent = 10;
        static constexpr std::uint32_t DefaultSamplingIntervalMilliseconds = 1'000;
        static constexpr std::uint32_t DefaultShutdownTimeoutMilliseconds = 10'000;
    };

    struct BenchmarkWorldHostControllerConfigV1 final
    {
        std::string executablePath;
        std::string serverConfigPath;
        std::string runsRoot;
        std::string clientAddress;
        std::uint16_t clientPort = BenchmarkWorldHostControllerConfigContract::DefaultClientPort;
        std::uint32_t clientCount = BenchmarkWorldHostControllerConfigContract::DefaultClientCount;
        std::uint32_t clientRampPerSecond = BenchmarkWorldHostControllerConfigContract::DefaultClientRampPerSecond;
        std::uint32_t clientAdmissionTimeoutMilliseconds =
            BenchmarkWorldHostControllerConfigContract::DefaultClientAdmissionTimeoutMilliseconds;
        std::uint32_t clientRoundResultTimeoutMilliseconds =
            BenchmarkWorldHostControllerConfigContract::DefaultClientRoundResultTimeoutMilliseconds;
        std::uint32_t clientEventQueueCapacity =
            BenchmarkWorldHostControllerConfigContract::DefaultClientEventQueueCapacity;
        std::uint32_t clientPayloadQueueCapacity =
            BenchmarkWorldHostControllerConfigContract::DefaultClientPayloadQueueCapacity;
        std::uint32_t workloadSeed = BenchmarkWorldHostControllerConfigContract::DefaultWorkloadSeed;
        std::uint32_t controlIntervalMilliseconds =
            BenchmarkWorldHostControllerConfigContract::DefaultControlIntervalMilliseconds;
        std::uint32_t boostPercent = BenchmarkWorldHostControllerConfigContract::DefaultBoostPercent;
        std::uint32_t samplingIntervalMilliseconds =
            BenchmarkWorldHostControllerConfigContract::DefaultSamplingIntervalMilliseconds;
        std::uint32_t shutdownTimeoutMilliseconds =
            BenchmarkWorldHostControllerConfigContract::DefaultShutdownTimeoutMilliseconds;
    };

    struct BenchmarkWorldHostControllerConfigResolveResult final
    {
        BenchmarkWorldHostControllerConfigV1 config;
        std::string normalizedJson;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkWorldHostControllerConfig final
    {
    public:
        [[nodiscard]] static BenchmarkWorldHostControllerConfigResolveResult Resolve(std::string_view configPath);
    };
} // namespace psnr::benchmark
