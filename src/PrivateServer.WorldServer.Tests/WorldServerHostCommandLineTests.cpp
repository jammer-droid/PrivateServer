#include "pch.h"

#include "WorldServerHostCommandLine.h"

#include <string>

namespace psnr::world::tests
{
    TEST(WorldServerHostCommandLineTests, ParsesRequiredConfigAndOptionalRunIdInAnyOrder)
    {
        char executable[] = "host.exe";
        char runIdOption[] = "--run-id";
        char runId[] = "run-77";
        char configOption[] = "--config";
        char configPath[] = "config.json";
        char* arguments[]{executable, runIdOption, runId, configOption, configPath};

        WorldResult<host::WorldServerHostOptions> result = host::WorldServerHostCommandLine::Parse(5, arguments);

        ASSERT_TRUE(result.Succeeded());
        EXPECT_EQ(result.Value().requestedRunId, "run-77");
        EXPECT_EQ(result.Value().configPath, std::filesystem::path{"config.json"});
    }

    TEST(WorldServerHostCommandLineTests, AcceptsConfigWithoutRunId)
    {
        char executable[] = "host.exe";
        char configOption[] = "--config";
        char configPath[] = "config.json";
        char* arguments[]{executable, configOption, configPath};

        WorldResult<host::WorldServerHostOptions> result = host::WorldServerHostCommandLine::Parse(3, arguments);

        ASSERT_TRUE(result.Succeeded());
        EXPECT_TRUE(result.Value().requestedRunId.empty());
        EXPECT_EQ(result.Value().configPath, std::filesystem::path{"config.json"});
        EXPECT_FALSE(result.Value().IsChildControlled());
    }

    TEST(WorldServerHostCommandLineTests, ParsesChildControlContractWithAbsoluteRunsRoot)
    {
        char executable[] = "host.exe";
        char configOption[] = "--config";
        char configPath[] = "config.json";
        char runIdOption[] = "--run-id";
        char runId[] = "run-77";
        char runsRootOption[] = "--runs-root";
        char runsRoot[] = "C:\\benchmark\\runs";
        char commandPipeOption[] = "--command-pipe-handle";
        char commandPipe[] = "101";
        char eventPipeOption[] = "--event-pipe-handle";
        char eventPipe[] = "202";
        char* arguments[]{executable, configOption,      configPath,  runIdOption,     runId,    runsRootOption,
                          runsRoot,   commandPipeOption, commandPipe, eventPipeOption, eventPipe};

        WorldResult<host::WorldServerHostOptions> result = host::WorldServerHostCommandLine::Parse(11, arguments);

        ASSERT_TRUE(result.Succeeded());
        EXPECT_EQ(result.Value().runsRoot, std::filesystem::path{"C:\\benchmark\\runs"});
        EXPECT_EQ(result.Value().commandPipeHandle, static_cast<std::uint64_t>(101));
        EXPECT_EQ(result.Value().eventPipeHandle, static_cast<std::uint64_t>(202));
        EXPECT_TRUE(result.Value().IsChildControlled());
    }

    TEST(WorldServerHostCommandLineTests, RejectsIncompleteChildControlContract)
    {
        char executable[] = "host.exe";
        char configOption[] = "--config";
        char configPath[] = "config.json";
        char runIdOption[] = "--run-id";
        char runId[] = "run-77";
        char runsRootOption[] = "--runs-root";
        char absoluteRunsRoot[] = "C:\\benchmark\\runs";
        char relativeRunsRoot[] = "artifacts\\runs";
        char commandPipeOption[] = "--command-pipe-handle";
        char commandPipe[] = "101";
        char eventPipeOption[] = "--event-pipe-handle";
        char eventPipe[] = "202";
        char zeroHandle[] = "0";
        char* missingEventPipe[]{executable,     configOption,     configPath,        runIdOption, runId,
                                 runsRootOption, absoluteRunsRoot, commandPipeOption, commandPipe};
        char* missingRunId[]{executable,        configOption, configPath,      runsRootOption, absoluteRunsRoot,
                             commandPipeOption, commandPipe,  eventPipeOption, eventPipe};
        char* missingRunsRoot[]{executable,        configOption, configPath,      runIdOption, runId,
                                commandPipeOption, commandPipe,  eventPipeOption, eventPipe};
        char* relativeRoot[]{executable, configOption, configPath, runsRootOption, relativeRunsRoot};
        char* invalidHandle[]{executable, configOption,    configPath,       runIdOption,
                              runId,      runsRootOption,  absoluteRunsRoot, commandPipeOption,
                              zeroHandle, eventPipeOption, eventPipe};

        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(9, missingEventPipe).Failed());
        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(9, missingRunId).Failed());
        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(9, missingRunsRoot).Failed());
        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(5, relativeRoot).Failed());
        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(11, invalidHandle).Failed());
    }

    TEST(WorldServerHostCommandLineTests, RejectsMissingConfigDuplicateOptionsAndUnknownOptions)
    {
        char executable[] = "host.exe";
        char runIdOption[] = "--run-id";
        char runId[] = "run-77";
        char configOption[] = "--config";
        char firstConfig[] = "first.json";
        char secondConfig[] = "second.json";
        char unknownOption[] = "--unknown";
        char* missingConfig[]{executable, runIdOption, runId};
        char* duplicateConfig[]{executable, configOption, firstConfig, configOption, secondConfig};
        char* unknown[]{executable, unknownOption};

        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(3, missingConfig).Failed());
        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(5, duplicateConfig).Failed());
        EXPECT_TRUE(host::WorldServerHostCommandLine::Parse(2, unknown).Failed());
    }
} // namespace psnr::world::tests
