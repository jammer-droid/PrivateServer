#include "pch.h"

#include "ApplicationRunArtifacts.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace psnr::logging
{
    namespace
    {
        using ManifestJson = nlohmann::ordered_json;

        class TemporaryArtifactRoot final
        {
        public:
            TemporaryArtifactRoot()
            {
                static std::atomic<std::uint64_t> nextSequence{0};

                std::error_code filesystemError;
                const std::filesystem::path temporaryRoot = std::filesystem::temp_directory_path(filesystemError);
                if (filesystemError)
                {
                    return;
                }

                for (std::size_t attempt = 0; attempt < 100; ++attempt)
                {
                    const std::uint64_t sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
                    const std::string directoryName = "psnr-application-run-artifacts-" +
                                                      std::to_string(GetCurrentProcessId()) + "-" +
                                                      std::to_string(sequence);
                    const std::filesystem::path candidate = temporaryRoot / directoryName;
                    if (std::filesystem::create_directory(candidate, filesystemError))
                    {
                        path_ = candidate;
                        return;
                    }
                    filesystemError.clear();
                }
            }

            ~TemporaryArtifactRoot() noexcept
            {
                if (path_.empty())
                {
                    return;
                }

                std::error_code filesystemError;
                std::filesystem::remove_all(path_, filesystemError);
            }

            TemporaryArtifactRoot(const TemporaryArtifactRoot&) = delete;
            TemporaryArtifactRoot& operator=(const TemporaryArtifactRoot&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const noexcept
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
        };

        [[nodiscard]] ApplicationRunArtifactConfig ValidConfig(const std::filesystem::path& runsRoot)
        {
            ApplicationRunArtifactConfig config{};
            config.runsRoot = runsRoot;
            config.requestedRunId = "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000";
            config.process = "world_server_host";
            config.buildConfiguration = "Debug";
            return config;
        }

        [[nodiscard]] ManifestJson ReadManifest(const std::filesystem::path& path)
        {
            std::ifstream input{path, std::ios::binary};
            return ManifestJson::parse(input);
        }
    } // namespace

    TEST(ApplicationRunArtifactsTests, RejectsInvalidInputWithoutChangingOutput)
    {
        TemporaryArtifactRoot temporaryRoot;
        ASSERT_FALSE(temporaryRoot.Path().empty());

        ApplicationRunArtifacts output{};
        output.runId = "preserved";
        ApplicationRunArtifactConfig invalidConfig = ValidConfig(temporaryRoot.Path() / "runs");
        invalidConfig.queueCapacity = 0;

        EXPECT_EQ(ApplicationRunArtifactFactory::Prepare(invalidConfig, &output),
                  ApplicationRunArtifactResult::InvalidConfig);
        EXPECT_EQ(output.runId, "preserved");
        EXPECT_EQ(ApplicationRunArtifactFactory::Prepare(ValidConfig(temporaryRoot.Path() / "runs"), nullptr),
                  ApplicationRunArtifactResult::InvalidArgument);
    }

    TEST(ApplicationRunArtifactsTests, PreparesNewRunDirectoriesAndManifest)
    {
        TemporaryArtifactRoot temporaryRoot;
        ASSERT_FALSE(temporaryRoot.Path().empty());
        const ApplicationRunArtifactConfig config = ValidConfig(temporaryRoot.Path() / "runs");
        ApplicationRunArtifacts artifacts{};

        ASSERT_EQ(ApplicationRunArtifactFactory::Prepare(config, &artifacts),
                  ApplicationRunArtifactResult::Success);

        EXPECT_EQ(artifacts.runId, config.requestedRunId);
        EXPECT_EQ(artifacts.runDirectory, config.runsRoot / config.requestedRunId);
        EXPECT_EQ(artifacts.worldDirectory, artifacts.runDirectory / "world");
        EXPECT_EQ(artifacts.runtimeDirectory, artifacts.runDirectory / "runtime");
        EXPECT_EQ(artifacts.applicationLogPath, artifacts.worldDirectory / "application.jsonl");
        EXPECT_EQ(artifacts.runtimeDiagnosticsPath, artifacts.runtimeDirectory / "diagnostics.jsonl");
        EXPECT_TRUE(std::filesystem::is_directory(artifacts.worldDirectory));
        EXPECT_TRUE(std::filesystem::is_directory(artifacts.runtimeDirectory));
        EXPECT_FALSE(std::filesystem::exists(artifacts.runDirectory / "benchmark"));
        EXPECT_TRUE(ApplicationLogConfig::IsValid(artifacts.applicationLogConfig));
        EXPECT_EQ(artifacts.applicationLogConfig.runId, config.requestedRunId);
        EXPECT_EQ(artifacts.applicationLogConfig.process, config.process);
        EXPECT_EQ(artifacts.applicationLogConfig.outputDirectory, artifacts.worldDirectory);
        EXPECT_EQ(artifacts.applicationLogConfig.minimumSeverity, config.minimumSeverity);
        EXPECT_EQ(artifacts.applicationLogConfig.queueCapacity, config.queueCapacity);
        EXPECT_EQ(artifacts.applicationLogConfig.rotationBytes, config.rotationBytes);
        EXPECT_EQ(artifacts.applicationLogConfig.rotationFileCount, config.rotationFileCount);
        EXPECT_EQ(artifacts.applicationLogPath,
                  artifacts.applicationLogConfig.outputDirectory / "application.jsonl");

        const ManifestJson manifest = ReadManifest(artifacts.manifestPath);
        EXPECT_EQ(manifest.at("schema"), "ps.run.manifest");
        EXPECT_EQ(manifest.at("version"), 1);
        EXPECT_EQ(manifest.at("runId"), config.requestedRunId);
        EXPECT_FALSE(manifest.at("startedAtUtc").get<std::string>().empty());
        EXPECT_EQ(manifest.at("executable").at("process"), config.process);
        EXPECT_EQ(manifest.at("executable").at("buildConfiguration"), config.buildConfiguration);
        EXPECT_EQ(manifest.at("applicationLogging").at("minimumSeverity"), "info");
        EXPECT_EQ(manifest.at("applicationLogging").at("queueCapacity"), config.queueCapacity);
        EXPECT_EQ(manifest.at("applicationLogging").at("rotationBytes"), config.rotationBytes);
        EXPECT_EQ(manifest.at("applicationLogging").at("rotationFileCount"), config.rotationFileCount);
        EXPECT_EQ(manifest.at("applicationLogging").at("path"), "world/application.jsonl");
        EXPECT_EQ(manifest.at("runtimeDiagnostics").at("path"), "runtime/diagnostics.jsonl");
    }

    TEST(ApplicationRunArtifactsTests, RejectsExistingRunDirectoryWithoutOverwritingManifest)
    {
        TemporaryArtifactRoot temporaryRoot;
        ASSERT_FALSE(temporaryRoot.Path().empty());
        const ApplicationRunArtifactConfig config = ValidConfig(temporaryRoot.Path() / "runs");
        ApplicationRunArtifacts firstArtifacts{};
        ASSERT_EQ(ApplicationRunArtifactFactory::Prepare(config, &firstArtifacts),
                  ApplicationRunArtifactResult::Success);
        const ManifestJson firstManifest = ReadManifest(firstArtifacts.manifestPath);

        ApplicationRunArtifacts secondArtifacts{};
        secondArtifacts.runId = "preserved";
        EXPECT_EQ(ApplicationRunArtifactFactory::Prepare(config, &secondArtifacts),
                  ApplicationRunArtifactResult::RunDirectoryAlreadyExists);
        EXPECT_EQ(secondArtifacts.runId, "preserved");
        EXPECT_EQ(ReadManifest(firstArtifacts.manifestPath), firstManifest);
    }

    TEST(ApplicationRunArtifactsTests, RejectsRequestedRunIdWithInvalidUtcTimestamp)
    {
        TemporaryArtifactRoot temporaryRoot;
        ASSERT_FALSE(temporaryRoot.Path().empty());
        const std::array<std::string, 4> invalidRunIds = {
            "run-20261301T153012.482Z-550e8400-e29b-41d4-a716-446655440000",
            "run-20260230T153012.482Z-550e8400-e29b-41d4-a716-446655440000",
            "run-20260801T243012.482Z-550e8400-e29b-41d4-a716-446655440000",
            "run-20260801T156012.482Z-550e8400-e29b-41d4-a716-446655440000",
        };

        for (const std::string& invalidRunId : invalidRunIds)
        {
            ApplicationRunArtifactConfig config = ValidConfig(temporaryRoot.Path() / "runs");
            config.requestedRunId = invalidRunId;
            ApplicationRunArtifacts artifacts{};
            artifacts.runId = "preserved";

            EXPECT_EQ(ApplicationRunArtifactFactory::Prepare(config, &artifacts),
                      ApplicationRunArtifactResult::InvalidConfig);
            EXPECT_EQ(artifacts.runId, "preserved");
        }
    }

    TEST(ApplicationRunArtifactsTests, GeneratesCanonicalRunIdWhenNoneIsRequested)
    {
        TemporaryArtifactRoot temporaryRoot;
        ASSERT_FALSE(temporaryRoot.Path().empty());
        ApplicationRunArtifactConfig config = ValidConfig(temporaryRoot.Path() / "runs");
        config.requestedRunId.clear();
        ApplicationRunArtifacts artifacts{};

        ASSERT_EQ(ApplicationRunArtifactFactory::Prepare(config, &artifacts),
                  ApplicationRunArtifactResult::Success);

        EXPECT_TRUE(ApplicationLogConfig::IsValid(artifacts.applicationLogConfig));
        EXPECT_EQ(artifacts.runId.size(), 61);
        EXPECT_EQ(artifacts.runId.substr(0, 4), "run-");
        EXPECT_EQ(artifacts.runId[12], 'T');
        EXPECT_EQ(artifacts.runId[19], '.');
        EXPECT_EQ(artifacts.runId[23], 'Z');
    }
} // namespace psnr::logging
