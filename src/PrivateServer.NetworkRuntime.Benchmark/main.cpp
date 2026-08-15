#include "BenchmarkCommandLine.h"
#include "BenchmarkConfigSource.h"
#include "BenchmarkProtocol.h"
#include "BenchmarkRunController.h"
#include "BenchmarkServeController.h"
#include "BenchmarkServerChildRunner.h"
#include "BenchmarkWorldHostController.h"
#include "BenchmarkWorldHostControllerConfig.h"

#include <iostream>

int main(const int argumentCount, char* arguments[])
{
    const psnr::benchmark::BenchmarkCommandLineParseResult parseResult =
        psnr::benchmark::BenchmarkCommandLineParser::Parse(argumentCount, arguments);
    if (!parseResult.Succeeded())
    {
        std::cerr << parseResult.error << '\n' << psnr::benchmark::BenchmarkCommandLineParser::Usage() << '\n';
        return 1;
    }

    if (parseResult.options.mode == psnr::benchmark::BenchmarkMode::WorldHostLifecycle)
    {
        const psnr::benchmark::BenchmarkWorldHostControllerConfigResolveResult configResult =
            psnr::benchmark::BenchmarkWorldHostControllerConfig::Resolve(parseResult.options.configPath);
        if (!configResult.Succeeded())
        {
            std::cerr << configResult.error << '\n';
            return 1;
        }
        return psnr::benchmark::BenchmarkWorldHostController::Run(configResult.config, configResult.normalizedJson);
    }

    const psnr::benchmark::BenchmarkConfigResolveResult configResult =
        psnr::benchmark::BenchmarkConfigSource::Resolve(parseResult.options.configPath);
    if (!configResult.Succeeded())
    {
        std::cerr << configResult.error << '\n';
        return 1;
    }

    // 실행 모드에 따른 실행 분기처리
    if (parseResult.options.mode == psnr::benchmark::BenchmarkMode::ServerChild) // for Child Process running
    {
        return psnr::benchmark::BenchmarkServerChildRunner::Run(
            parseResult.options.runId, configResult.resolved.config.server, configResult.resolved.config.artifact,
            parseResult.options.commandPipeHandle, parseResult.options.eventPipeHandle);
    }

    if (parseResult.options.mode == psnr::benchmark::BenchmarkMode::Run)
    {
        return psnr::benchmark::BenchmarkRunController::Run(
            configResult.resolved.config, configResult.resolved.normalizedJson, parseResult.options.configPath);
    }

    // serve mode
    return psnr::benchmark::BenchmarkServeController::Run(
        configResult.resolved.config, configResult.resolved.normalizedJson, parseResult.options.configPath);
}
