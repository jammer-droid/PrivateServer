#include "pch.h"

#include <PrivateServer/NetworkRuntime/Version.h>

TEST(NetworkRuntimeTests, GetVersionReturnsRuntimeAbiVersion)
{
    EXPECT_EQ(nr_get_version(), 1);
}
