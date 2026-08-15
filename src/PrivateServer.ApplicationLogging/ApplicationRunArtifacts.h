#pragma once

#include "ApplicationLogConfig.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace psnr::logging
{
    enum class ApplicationRunArtifactResult : std::uint8_t
    {
        Success,
        InvalidArgument,
        InvalidConfig,
        IdentityGenerationFailed,
        OutputRootCreationFailed,
        RunDirectoryAlreadyExists,
        RunDirectoryCreationFailed,
        SubdirectoryCreationFailed,
        ManifestWriteFailed,
        ResourceUnavailable,
    };

    struct ApplicationRunArtifactConfig final
    {
        std::filesystem::path runsRoot = std::filesystem::path{"artifacts"} / "runs";
        std::string requestedRunId;
        std::string process;
        std::string buildConfiguration;
        ApplicationLogSeverity minimumSeverity = ApplicationLogSeverity::Info;
        std::size_t queueCapacity = DefaultApplicationLogQueueCapacity;
        std::uint64_t rotationBytes = DefaultApplicationLogRotationBytes;
        std::size_t rotationFileCount = DefaultApplicationLogRotationFileCount;
    };

    struct ApplicationRunArtifacts final
    {
        std::string runId;
        std::string startedAtUtc;
        std::filesystem::path runDirectory;
        std::filesystem::path worldDirectory;
        std::filesystem::path runtimeDirectory;
        std::filesystem::path manifestPath;
        std::filesystem::path applicationLogPath;
        std::filesystem::path runtimeDiagnosticsPath;
        ApplicationLogConfig applicationLogConfig;
    };

    class ApplicationRunArtifactFactory final
    {
    public:
        [[nodiscard]] static ApplicationRunArtifactResult Prepare(const ApplicationRunArtifactConfig& config,
                                                                  ApplicationRunArtifacts* outArtifacts) noexcept;
    };
} // namespace psnr::logging
