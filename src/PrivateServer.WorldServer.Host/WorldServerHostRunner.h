#pragma once

#include "ApplicationLogHandle.h"
#include "WorldServerHostConfig.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace psnr::core
{
    class NrStatus;
}

namespace psnr::runtime
{
    class NrGateway;
    class NrServer;
} // namespace psnr::runtime

namespace psnr::world
{
    class WorldExecutionStorage;
}

namespace psnr::world::host
{
    class WorldServerHostLog;
    class WorldServerHostArtifactWriteQueue;
    class WorldServerHostChildControl;
    class WorldServerHostStopSignal;

    class WorldServerHostRunner final
    {
    public:
        [[nodiscard]] static int Run(const WorldServerHostConfig& config,
                                     const std::filesystem::path& runtimeDiagnosticsPath,
                                     const std::filesystem::path& worldArtifactDirectory, std::string_view runId,
                                     const WorldServerHostLog& log,
                                     psnr::logging::ApplicationLogHandle worldApplicationLog,
                                     WorldServerHostChildControl* childControl, WorldServerHostStopSignal& stopSignal);

    private:
        class RuntimeShutdownAdapter;

        [[nodiscard]] static int ReportRuntimeFailure(const WorldServerHostLog& log, std::string_view operation,
                                                      psnr::core::NrStatus status);
        static void StopRuntimeAfterStartupFailure(const WorldServerHostLog& log, psnr::runtime::NrServer& server);
        [[nodiscard]] static bool CaptureAndSubmitRuntimeSample(psnr::runtime::NrServer& server,
                                                                WorldServerHostArtifactWriteQueue& artifactWriteQueue,
                                                                const WorldServerHostLog& log, std::uint64_t sequence,
                                                                std::uint64_t elapsedMilliseconds,
                                                                std::string_view captureOperation);
        [[nodiscard]] static int RunWorld(psnr::runtime::NrServer& server, psnr::runtime::NrGateway& gateway,
                                          WorldExecutionStorage& storage, const WorldServerHostLog& log,
                                          psnr::logging::ApplicationLogHandle worldApplicationLog,
                                          const WorldServerHostConfig& config,
                                          const std::filesystem::path& worldArtifactDirectory,
                                          const std::filesystem::path& runtimeReportPath, std::string_view runId,
                                          WorldServerHostChildControl* childControl,
                                          WorldServerHostStopSignal& stopSignal);
    };
} // namespace psnr::world::host
