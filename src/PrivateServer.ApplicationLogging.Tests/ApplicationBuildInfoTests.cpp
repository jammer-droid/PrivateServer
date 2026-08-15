#include "pch.h"

#include "ApplicationBuildInfo.h"

#include <string_view>

namespace psnr::logging
{
    TEST(ApplicationBuildInfoTests, ReportsCurrentBuildIdentity)
    {
        const ApplicationBuildInfo info = ApplicationBuildInfo::Current();

        EXPECT_TRUE(info.configuration == "Debug" || info.configuration == "Release");
        EXPECT_TRUE(info.architecture == "x64" || info.architecture == "Win32" || info.architecture == "unknown");
        EXPECT_GT(info.msvcVersion, 0U);
        EXPECT_GT(info.msvcFullVersion, 0U);
    }
} // namespace psnr::logging
