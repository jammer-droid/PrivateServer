#include "pch.h"

#include "ApplicationRunArtifacts.h"

#include "ApplicationLogValidation.h"

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <objbase.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#pragma comment(lib, "Ole32.lib")

namespace psnr::logging
{
    namespace
    {
        using ManifestJson = nlohmann::ordered_json;

        constexpr std::string_view ManifestSchema = "ps.run.manifest";
        constexpr std::uint64_t ManifestVersion = 1;
        constexpr std::string_view ApplicationLogRelativePath = "world/application.jsonl";
        constexpr std::string_view RuntimeDiagnosticsRelativePath = "runtime/diagnostics.jsonl";

        [[nodiscard]] std::string_view SeverityName(const ApplicationLogSeverity severity) noexcept
        {
            switch (severity)
            {
            case ApplicationLogSeverity::Debug:
                return "debug";
            case ApplicationLogSeverity::Info:
                return "info";
            case ApplicationLogSeverity::Warning:
                return "warning";
            case ApplicationLogSeverity::Error:
                return "error";
            case ApplicationLogSeverity::Critical:
                return "critical";
            default:
                return {};
            }
        }

        [[nodiscard]] bool TryFormatTimestamp(const SYSTEMTIME& time, std::string* const outStartedAtUtc,
                                              std::string* const outRunTimestamp)
        {
            std::array<char, 25> startedAtText{};
            const int startedAtLength = std::snprintf(
                startedAtText.data(), startedAtText.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                static_cast<unsigned int>(time.wYear), static_cast<unsigned int>(time.wMonth),
                static_cast<unsigned int>(time.wDay), static_cast<unsigned int>(time.wHour),
                static_cast<unsigned int>(time.wMinute), static_cast<unsigned int>(time.wSecond),
                static_cast<unsigned int>(time.wMilliseconds));

            std::array<char, 21> runTimestampText{};
            const int runTimestampLength = std::snprintf(
                runTimestampText.data(), runTimestampText.size(), "%04u%02u%02uT%02u%02u%02u.%03uZ",
                static_cast<unsigned int>(time.wYear), static_cast<unsigned int>(time.wMonth),
                static_cast<unsigned int>(time.wDay), static_cast<unsigned int>(time.wHour),
                static_cast<unsigned int>(time.wMinute), static_cast<unsigned int>(time.wSecond),
                static_cast<unsigned int>(time.wMilliseconds));

            if (startedAtLength != 24 || runTimestampLength != 20)
            {
                return false;
            }

            std::string startedAtUtc{startedAtText.data(), static_cast<std::size_t>(startedAtLength)};
            std::string runTimestamp{runTimestampText.data(), static_cast<std::size_t>(runTimestampLength)};
            *outStartedAtUtc = std::move(startedAtUtc);
            *outRunTimestamp = std::move(runTimestamp);
            return true;
        }

        [[nodiscard]] bool TryCreateUuid(std::string* const outUuid)
        {
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

            std::string uuid;
            uuid.reserve(36);
            for (std::size_t index = 1; index < 37; ++index)
            {
                const unsigned char character = static_cast<unsigned char>(guidText[index]);
                uuid.push_back(static_cast<char>(std::tolower(character)));
            }

            *outUuid = std::move(uuid);
            return true;
        }

        [[nodiscard]] bool TryPrepareIdentity(const std::string& requestedRunId, std::string* const outRunId,
                                              std::string* const outStartedAtUtc)
        {
            SYSTEMTIME time{};
            GetSystemTime(&time);

            std::string startedAtUtc;
            std::string runTimestamp;
            if (!TryFormatTimestamp(time, &startedAtUtc, &runTimestamp))
            {
                return false;
            }

            std::string runId = requestedRunId;
            if (runId.empty())
            {
                std::string uuid;
                if (!TryCreateUuid(&uuid))
                {
                    return false;
                }
                runId = "run-" + runTimestamp + "-" + uuid;
            }

            if (!internal::MatchesCanonicalRunId(runId))
            {
                return false;
            }

            *outRunId = std::move(runId);
            *outStartedAtUtc = std::move(startedAtUtc);
            return true;
        }

        [[nodiscard]] ManifestJson CreateManifest(const ApplicationRunArtifactConfig& config,
                                                  const ApplicationRunArtifacts& artifacts)
        {
            ManifestJson executable = ManifestJson::object();
            executable["process"] = config.process;
            executable["buildConfiguration"] = config.buildConfiguration;

            ManifestJson applicationLogging = ManifestJson::object();
            applicationLogging["minimumSeverity"] = std::string(SeverityName(config.minimumSeverity));
            applicationLogging["queueCapacity"] = config.queueCapacity;
            applicationLogging["rotationBytes"] = config.rotationBytes;
            applicationLogging["rotationFileCount"] = config.rotationFileCount;
            applicationLogging["path"] = ApplicationLogRelativePath;

            ManifestJson runtimeDiagnostics = ManifestJson::object();
            runtimeDiagnostics["path"] = RuntimeDiagnosticsRelativePath;

            ManifestJson manifest = ManifestJson::object();
            manifest["schema"] = ManifestSchema;
            manifest["version"] = ManifestVersion;
            manifest["runId"] = artifacts.runId;
            manifest["startedAtUtc"] = artifacts.startedAtUtc;
            manifest["executable"] = std::move(executable);
            manifest["applicationLogging"] = std::move(applicationLogging);
            manifest["runtimeDiagnostics"] = std::move(runtimeDiagnostics);
            return manifest;
        }

        [[nodiscard]] bool TryWriteManifest(const ApplicationRunArtifactConfig& config,
                                            const ApplicationRunArtifacts& artifacts)
        {
            std::ofstream output{artifacts.manifestPath, std::ios::binary | std::ios::trunc};
            if (!output.is_open())
            {
                return false;
            }

            const std::string payload = CreateManifest(config, artifacts).dump();
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            output.put('\n');
            output.flush();
            return output.good();
        }

        [[nodiscard]] bool IsValidConfig(const ApplicationRunArtifactConfig& config) noexcept
        {
            return !config.runsRoot.empty() && !config.process.empty() && !config.buildConfiguration.empty() &&
                   internal::IsValidSeverity(config.minimumSeverity) && config.queueCapacity > 0 &&
                   config.rotationBytes > 0 && config.rotationFileCount > 0 &&
                   config.rotationFileCount <= MaximumApplicationLogRotationFileCount;
        }
    } // namespace

    ApplicationRunArtifactResult ApplicationRunArtifactFactory::Prepare(
        const ApplicationRunArtifactConfig& config, ApplicationRunArtifacts* const outArtifacts) noexcept
    {
        if (outArtifacts == nullptr)
        {
            return ApplicationRunArtifactResult::InvalidArgument;
        }
        if (!IsValidConfig(config))
        {
            return ApplicationRunArtifactResult::InvalidConfig;
        }

        try
        {
            ApplicationRunArtifacts artifacts{};
            if (!TryPrepareIdentity(config.requestedRunId, &artifacts.runId, &artifacts.startedAtUtc))
            {
                return config.requestedRunId.empty() ? ApplicationRunArtifactResult::IdentityGenerationFailed
                                                     : ApplicationRunArtifactResult::InvalidConfig;
            }

            std::error_code filesystemError;
            std::filesystem::create_directories(config.runsRoot, filesystemError);
            if (filesystemError)
            {
                return ApplicationRunArtifactResult::OutputRootCreationFailed;
            }

            artifacts.runDirectory = config.runsRoot / artifacts.runId;
            const bool runDirectoryCreated = std::filesystem::create_directory(artifacts.runDirectory, filesystemError);
            if (filesystemError)
            {
                return ApplicationRunArtifactResult::RunDirectoryCreationFailed;
            }
            if (!runDirectoryCreated)
            {
                return ApplicationRunArtifactResult::RunDirectoryAlreadyExists;
            }

            artifacts.worldDirectory = artifacts.runDirectory / "world";
            artifacts.runtimeDirectory = artifacts.runDirectory / "runtime";
            const bool worldDirectoryCreated = std::filesystem::create_directory(artifacts.worldDirectory,
                                                                                  filesystemError);
            if (filesystemError || !worldDirectoryCreated)
            {
                return ApplicationRunArtifactResult::SubdirectoryCreationFailed;
            }
            const bool runtimeDirectoryCreated = std::filesystem::create_directory(artifacts.runtimeDirectory,
                                                                                    filesystemError);
            if (filesystemError || !runtimeDirectoryCreated)
            {
                return ApplicationRunArtifactResult::SubdirectoryCreationFailed;
            }

            artifacts.manifestPath = artifacts.runDirectory / "run-manifest.json";
            artifacts.applicationLogPath = artifacts.worldDirectory / "application.jsonl";
            artifacts.runtimeDiagnosticsPath = artifacts.runtimeDirectory / "diagnostics.jsonl";
            artifacts.applicationLogConfig = ApplicationLogConfig{
                artifacts.runId,
                config.process,
                artifacts.worldDirectory,
                config.minimumSeverity,
                config.queueCapacity,
                config.rotationBytes,
                config.rotationFileCount,
            };

            if (!ApplicationLogConfig::IsValid(artifacts.applicationLogConfig))
            {
                return ApplicationRunArtifactResult::InvalidConfig;
            }
            if (!TryWriteManifest(config, artifacts))
            {
                return ApplicationRunArtifactResult::ManifestWriteFailed;
            }

            *outArtifacts = std::move(artifacts);
            return ApplicationRunArtifactResult::Success;
        }
        catch (...)
        {
            return ApplicationRunArtifactResult::ResourceUnavailable;
        }
    }
} // namespace psnr::logging
