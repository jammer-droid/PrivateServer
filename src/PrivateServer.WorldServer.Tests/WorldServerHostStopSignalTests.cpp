#include "pch.h"

#include "WorldServerHostStopSignal.h"

namespace psnr::world::tests
{
    TEST(WorldServerHostStopSignalTests, ExposesARequestedProcessStop)
    {
        host::WorldServerHostStopSignal signal;

        signal.Request();

        EXPECT_TRUE(signal.IsRequested());
    }
} // namespace psnr::world::tests
