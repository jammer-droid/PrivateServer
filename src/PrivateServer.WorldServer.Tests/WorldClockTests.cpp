#include "pch.h"

#include "WorldClock.h"

#include <chrono>
#include <cstdint>
#include <limits>

namespace psnr::world::tests
{
    TEST(WorldClockTests, RoundsPositiveWaitUpToMilliseconds)
    {
        EXPECT_EQ(CalculateWorldWaitTimeoutMilliseconds(WorldClock::duration::zero()), 0u);
        EXPECT_EQ(CalculateWorldWaitTimeoutMilliseconds(std::chrono::nanoseconds{1}), 1u);
        EXPECT_EQ(CalculateWorldWaitTimeoutMilliseconds(std::chrono::milliseconds{16}), 16u);
        EXPECT_EQ(CalculateWorldWaitTimeoutMilliseconds(std::chrono::milliseconds{16} + std::chrono::nanoseconds{1}),
                  17u);
    }

    TEST(WorldClockTests, ClampsWaitToRuntimeTimeoutRange)
    {
        constexpr std::uint64_t MaximumTimeoutMilliseconds = std::numeric_limits<std::uint32_t>::max();
        const std::chrono::milliseconds overMaximum{MaximumTimeoutMilliseconds + 1};

        EXPECT_EQ(CalculateWorldWaitTimeoutMilliseconds(overMaximum), std::numeric_limits<std::uint32_t>::max());
    }
} // namespace psnr::world::tests
