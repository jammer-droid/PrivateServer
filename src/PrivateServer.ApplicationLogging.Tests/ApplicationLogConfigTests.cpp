#include "pch.h"

#include "ApplicationLogConfig.h"

#include <array>
#include <string_view>

namespace psnr::logging
{
    namespace
    {
        constexpr std::string_view ValidRunId =
            "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000";

        [[nodiscard]] ApplicationLogConfig MakeValidConfig()
        {
            ApplicationLogConfig config{};
            config.runId = ValidRunId;
            config.process = "world-server-host";
            config.outputDirectory = "artifacts/runs/test/world";
            return config;
        }
    } // namespace

    TEST(ApplicationLogConfigTests, DefaultsToCanonicalProductionBounds)
    {
        const ApplicationLogConfig config{};

        EXPECT_EQ(config.minimumSeverity, ApplicationLogSeverity::Info);
        EXPECT_EQ(config.queueCapacity, 8'192);
        EXPECT_EQ(config.rotationBytes, 10ULL * 1024ULL * 1024ULL);
        EXPECT_EQ(config.rotationFileCount, 5);
    }

    TEST(ApplicationLogConfigTests, AcceptsCanonicalConfig)
    {
        const ApplicationLogConfig config = MakeValidConfig();

        EXPECT_TRUE(ApplicationLogConfig::IsValid(config));
    }

    TEST(ApplicationLogConfigTests, RejectsMalformedRunId)
    {
        const std::array<std::string_view, 6> invalidRunIds{
            "",
            "20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000",
            "run-20260801-153012.482Z-550e8400-e29b-41d4-a716-446655440000",
            "run-20260801T153012,482Z-550e8400-e29b-41d4-a716-446655440000",
            "run-20260801T153012.482Z-550e8400e29b-41d4-a716-446655440000",
            "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-44665544000g",
        };

        for (const std::string_view invalidRunId : invalidRunIds)
        {
            ApplicationLogConfig config = MakeValidConfig();
            config.runId = invalidRunId;

            EXPECT_FALSE(ApplicationLogConfig::IsValid(config)) << invalidRunId;
        }
    }

    TEST(ApplicationLogConfigTests, RejectsMissingIdentityOrOutputDirectory)
    {
        ApplicationLogConfig missingProcess = MakeValidConfig();
        missingProcess.process.clear();

        ApplicationLogConfig missingOutputDirectory = MakeValidConfig();
        missingOutputDirectory.outputDirectory.clear();

        EXPECT_FALSE(ApplicationLogConfig::IsValid(missingProcess));
        EXPECT_FALSE(ApplicationLogConfig::IsValid(missingOutputDirectory));
    }

    TEST(ApplicationLogConfigTests, RejectsUnknownSeverityAndZeroBounds)
    {
        ApplicationLogConfig unknownSeverity = MakeValidConfig();
        unknownSeverity.minimumSeverity = static_cast<ApplicationLogSeverity>(5);

        ApplicationLogConfig zeroQueueCapacity = MakeValidConfig();
        zeroQueueCapacity.queueCapacity = 0;

        ApplicationLogConfig zeroRotationBytes = MakeValidConfig();
        zeroRotationBytes.rotationBytes = 0;

        ApplicationLogConfig zeroRotationFileCount = MakeValidConfig();
        zeroRotationFileCount.rotationFileCount = 0;

        ApplicationLogConfig excessiveRotationFileCount = MakeValidConfig();
        excessiveRotationFileCount.rotationFileCount = MaximumApplicationLogRotationFileCount + 1;

        EXPECT_FALSE(ApplicationLogConfig::IsValid(unknownSeverity));
        EXPECT_FALSE(ApplicationLogConfig::IsValid(zeroQueueCapacity));
        EXPECT_FALSE(ApplicationLogConfig::IsValid(zeroRotationBytes));
        EXPECT_FALSE(ApplicationLogConfig::IsValid(zeroRotationFileCount));
        EXPECT_FALSE(ApplicationLogConfig::IsValid(excessiveRotationFileCount));
    }
} // namespace psnr::logging
