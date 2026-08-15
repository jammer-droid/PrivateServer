#include "ApplicationBuildInfo.h"
#include "ApplicationLogger.h"
#include "ApplicationRunArtifacts.h"
#include "WorldServerHostCommandLine.h"
#include "WorldServerHostChildControl.h"
#include "WorldServerHostConfig.h"
#include "WorldServerHostLog.h"
#include "WorldServerHostRunner.h"
#include "WorldServerHostStopSignal.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view HostProcess = "world-server-host";
    constexpr std::string_view EffectiveConfigFileName = "effective-config.json";
} // namespace

int main(const int argumentCount, char* arguments[])
{
    psnr::world::WorldResult<psnr::world::host::WorldServerHostOptions> optionsResult =
        psnr::world::host::WorldServerHostCommandLine::Parse(argumentCount, arguments);
    if (optionsResult.Failed())
    {
        std::cerr << "usage: PrivateServer.WorldServer.Host.exe --config <path> "
                     "[--run-id <canonical-run-id>] [--runs-root <absolute-path>]\n";
        return 1;
    }
    psnr::world::host::WorldServerHostOptions options = optionsResult.TakeValue();

    const psnr::world::WorldResult<psnr::world::host::WorldServerHostConfig, std::string> configResult =
        psnr::world::host::WorldServerHostConfigSource::Load(options.configPath);
    if (configResult.Failed())
    {
        std::cerr << "WorldServerHostConfigSource::Load failed. error=" << configResult.Error() << '\n';
        return 1;
    }
    const psnr::world::host::WorldServerHostConfig& hostConfig = configResult.Value();

    const psnr::world::WorldResult<std::string, std::string> normalizedConfigResult =
        psnr::world::host::WorldServerHostConfigSource::SerializeNormalized(hostConfig);
    if (normalizedConfigResult.Failed())
    {
        std::cerr << normalizedConfigResult.Error() << '\n';
        return 1;
    }

    psnr::logging::ApplicationRunArtifactConfig artifactConfig{};
    const psnr::logging::ApplicationBuildInfo buildInfo = psnr::logging::ApplicationBuildInfo::Current();
    artifactConfig.requestedRunId = std::move(options.requestedRunId);
    artifactConfig.process = HostProcess;
    artifactConfig.buildConfiguration = buildInfo.configuration;
    artifactConfig.minimumSeverity = hostConfig.minimumLogSeverity;
    if (!options.runsRoot.empty())
    {
        artifactConfig.runsRoot = std::move(options.runsRoot);
    }

    psnr::logging::ApplicationRunArtifacts artifacts{};
    const psnr::logging::ApplicationRunArtifactResult artifactResult =
        psnr::logging::ApplicationRunArtifactFactory::Prepare(artifactConfig, &artifacts);
    if (artifactResult != psnr::logging::ApplicationRunArtifactResult::Success)
    {
        std::cerr << "ApplicationRunArtifactFactory::Prepare failed. result="
                  << static_cast<std::uint32_t>(artifactResult) << '\n';
        return 1;
    }

    const psnr::world::WorldResult<void, std::string> writeConfigResult =
        psnr::world::host::WorldServerHostConfigSource::WriteNormalized(
            artifacts.worldDirectory / EffectiveConfigFileName, normalizedConfigResult.Value());
    if (writeConfigResult.Failed())
    {
        std::cerr << writeConfigResult.Error() << '\n';
        return 1;
    }

    std::unique_ptr<psnr::logging::ApplicationLogger> applicationLogger;
    psnr::logging::ApplicationLoggerResult loggingResult =
        psnr::logging::ApplicationLogger::Create(artifacts.applicationLogConfig, &applicationLogger);
    if (loggingResult != psnr::logging::ApplicationLoggerResult::Success || applicationLogger == nullptr)
    {
        std::cerr << "ApplicationLogger::Create failed. result=" << static_cast<std::uint32_t>(loggingResult) << '\n';
        return 1;
    }

    loggingResult = applicationLogger->Start();
    if (loggingResult != psnr::logging::ApplicationLoggerResult::Success)
    {
        std::cerr << "ApplicationLogger::Start failed. result=" << static_cast<std::uint32_t>(loggingResult) << '\n';
        return 1;
    }
    const psnr::logging::ApplicationLogHandle worldLog = applicationLogger->CreateHandle();
    const psnr::world::host::WorldServerHostLog hostLog{*applicationLogger, worldLog};
    hostLog.HostEvent(psnr::logging::ApplicationLogSeverity::Info, "application_logging_started", "application_logging",
                      "started");

    // Logger and its borrowed World handle outlive every Runtime/World owner created by the runner.
    psnr::world::host::WorldServerHostStopSignal stopSignal;
    const psnr::world::WorldResult<void, std::uint32_t> stopSignalResult = stopSignal.RegisterConsoleControl();
    if (stopSignalResult.Failed())
    {
        hostLog.HostNativeFailure("console_control_registration_failed", "register_console_control", "native_failure",
                                  stopSignalResult.Error());
        return hostLog.Complete(1);
    }

    std::unique_ptr<psnr::world::host::WorldServerHostChildControl> childControl;
    if (options.IsChildControlled())
    {
        psnr::world::WorldResult<std::unique_ptr<psnr::world::host::WorldServerHostChildControl>,
                                 psnr::world::host::WorldServerHostChildControlFailure>
            childControlResult = psnr::world::host::WorldServerHostChildControl::Create(
                artifacts.runId, options.commandPipeHandle, options.eventPipeHandle);
        if (childControlResult.Failed())
        {
            hostLog.HostNativeFailure("child_control_create_failed", "child_control_create", "failed",
                                      childControlResult.Error().nativeErrorCode);
            return hostLog.Complete(1);
        }
        childControl = childControlResult.TakeValue();
    }

    const int result = psnr::world::host::WorldServerHostRunner::Run(hostConfig, artifacts.runtimeDiagnosticsPath,
                                                                     artifacts.worldDirectory, artifacts.runId, hostLog,
                                                                     worldLog, childControl.get(), stopSignal);
    return hostLog.Complete(result);
}
