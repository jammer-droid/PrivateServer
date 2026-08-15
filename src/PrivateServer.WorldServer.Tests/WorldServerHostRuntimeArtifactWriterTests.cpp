#include "pch.h"

#include "WorldServerHostRuntimeArtifactWriter.h"

#include <PrivateServer/NetworkRuntime/NrDiagnosticsConfig.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerConfig.h>
#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace psnr::world::tests
{
    namespace
    {
        class TemporaryRuntimeArtifactDirectory final
        {
        public:
            TemporaryRuntimeArtifactDirectory()
            {
                path_ = std::filesystem::temp_directory_path() /
                        ("private-server-runtime-report-" + std::to_string(GetCurrentProcessId()));
                std::error_code error;
                static_cast<void>(std::filesystem::remove_all(path_, error));
                valid_ = std::filesystem::create_directories(path_, error) && !error;
            }

            ~TemporaryRuntimeArtifactDirectory()
            {
                std::error_code error;
                static_cast<void>(std::filesystem::remove_all(path_, error));
            }

            TemporaryRuntimeArtifactDirectory(const TemporaryRuntimeArtifactDirectory&) = delete;
            TemporaryRuntimeArtifactDirectory& operator=(const TemporaryRuntimeArtifactDirectory&) = delete;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return valid_;
            }

            [[nodiscard]] const std::filesystem::path& Path() const noexcept
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
            bool valid_ = false;
        };
    } // namespace

    TEST(WorldServerHostRuntimeArtifactWriterTests, WritesCompleteTerminalSnapshotAfterRuntimeShutdown)
    {
        const TemporaryRuntimeArtifactDirectory directory;
        ASSERT_TRUE(directory.IsValid());
        psnr::runtime::NrServerConfig config;
        config.bindEndpoint.port = 27015;
        config.diagnostics.mode = psnr::runtime::NrDiagnosticsMode::Debug;
        psnr::runtime::NrServer server;
        ASSERT_TRUE(psnr::runtime::NrServer::Create(config, &server).Succeeded());
        ASSERT_TRUE(server.Shutdown().Succeeded());
        psnr::runtime::NrServerSnapshot snapshot;
        const psnr::core::NrStatus captureStatus = server.CaptureSnapshot(&snapshot);
        ASSERT_TRUE(captureStatus.Succeeded());
        const std::filesystem::path outputPath = directory.Path() / "report.json";

        const WorldResult<void, host::WorldServerHostRuntimeArtifactWriteError> writeResult =
            host::WorldServerHostRuntimeArtifactWriter::Write("run-1", outputPath, captureStatus, snapshot);

        ASSERT_TRUE(writeResult.Succeeded());
        std::ifstream input(outputPath, std::ios::binary);
        ASSERT_TRUE(input.is_open());
        const nlohmann::ordered_json report = nlohmann::ordered_json::parse(input);
        EXPECT_EQ(report["schema"], "psnr.runtime.report");
        EXPECT_EQ(report["version"], 1);
        EXPECT_EQ(report["runId"], "run-1");
        EXPECT_EQ(report["status"], "complete");
        EXPECT_EQ(report["snapshotCaptureSucceeded"], true);
        EXPECT_EQ(report["snapshotValid"], true);
        EXPECT_EQ(report["lifecycleState"], "shutdown");
        EXPECT_EQ(report["registeredSessionCount"], 0);
        EXPECT_EQ(report["pendingIoCount"], 0);
        EXPECT_EQ(report["sendMailboxDepth"], 0);
        EXPECT_EQ(report["pendingSendQueueDepth"], 0);
        EXPECT_EQ(report["toWorldEventDepth"], 0);
        EXPECT_TRUE(report["memoryPools"].contains("recvBuffer"));
        EXPECT_TRUE(report["poolPressure"].contains("payload256"));
        EXPECT_TRUE(report["pressureTransactions"].contains("sendAdmissionRejected"));
        EXPECT_EQ(report["diagnostics"]["enabled"], true);
        EXPECT_EQ(report["diagnostics"]["sinkFailed"], false);
    }

    TEST(WorldServerHostRuntimeArtifactWriterTests, WritesIncompleteReportForFailedSnapshotCapture)
    {
        const TemporaryRuntimeArtifactDirectory directory;
        ASSERT_TRUE(directory.IsValid());
        const psnr::runtime::NrServerSnapshot snapshot;
        const psnr::core::NrStatus captureStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
        const std::filesystem::path outputPath = directory.Path() / "report.json";

        const WorldResult<void, host::WorldServerHostRuntimeArtifactWriteError> writeResult =
            host::WorldServerHostRuntimeArtifactWriter::Write("run-2", outputPath, captureStatus, snapshot);

        ASSERT_TRUE(writeResult.Succeeded());
        std::ifstream input(outputPath, std::ios::binary);
        ASSERT_TRUE(input.is_open());
        const nlohmann::ordered_json report = nlohmann::ordered_json::parse(input);
        EXPECT_EQ(report["status"], "incomplete");
        EXPECT_EQ(report["snapshotCaptureSucceeded"], false);
        EXPECT_EQ(report["snapshotValid"], false);
        EXPECT_EQ(report["lifecycleState"], "invalid");
    }

    TEST(WorldServerHostRuntimeArtifactWriterTests, RejectsMissingRunIdentityOrOutputPath)
    {
        const psnr::runtime::NrServerSnapshot snapshot;
        const psnr::core::NrStatus captureStatus = psnr::core::NrStatus::Success();

        const WorldResult<void, host::WorldServerHostRuntimeArtifactWriteError> missingRunId =
            host::WorldServerHostRuntimeArtifactWriter::Write({}, "report.json", captureStatus, snapshot);
        const WorldResult<void, host::WorldServerHostRuntimeArtifactWriteError> missingPath =
            host::WorldServerHostRuntimeArtifactWriter::Write("run-1", {}, captureStatus, snapshot);

        ASSERT_TRUE(missingRunId.Failed());
        EXPECT_EQ(missingRunId.Error(), host::WorldServerHostRuntimeArtifactWriteError::InvalidArgument);
        ASSERT_TRUE(missingPath.Failed());
        EXPECT_EQ(missingPath.Error(), host::WorldServerHostRuntimeArtifactWriteError::InvalidArgument);
    }
} // namespace psnr::world::tests
