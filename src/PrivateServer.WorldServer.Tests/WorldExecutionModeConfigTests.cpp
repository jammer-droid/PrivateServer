#include "pch.h"

#include "WorldExecutionModeConfig.h"

#include <cstdint>

namespace psnr::world
{
    TEST(WorldExecutionModeConfigTests, DefaultsBothDirectionsToDoubleBuffered)
    {
        constexpr WorldExecutionModeConfig config{};

        EXPECT_EQ(config.inboundMode, WorldInboundMode::DoubleBuffered);
        EXPECT_EQ(config.outboundMode, WorldOutboundMode::DoubleBuffered);
        EXPECT_TRUE(IsValid(config));
    }

    TEST(WorldExecutionModeConfigTests, AcceptsEveryIndependentModeCombination)
    {
        constexpr WorldExecutionModeConfig targetServerTickDirect{
            WorldInboundMode::TargetServerTick,
            WorldOutboundMode::Direct,
        };
        constexpr WorldExecutionModeConfig targetServerTickDoubleBuffered{
            WorldInboundMode::TargetServerTick,
            WorldOutboundMode::DoubleBuffered,
        };
        constexpr WorldExecutionModeConfig doubleBufferedDirect{
            WorldInboundMode::DoubleBuffered,
            WorldOutboundMode::Direct,
        };
        constexpr WorldExecutionModeConfig doubleBufferedDoubleBuffered{
            WorldInboundMode::DoubleBuffered,
            WorldOutboundMode::DoubleBuffered,
        };

        EXPECT_TRUE(IsValid(targetServerTickDirect));
        EXPECT_TRUE(IsValid(targetServerTickDoubleBuffered));
        EXPECT_TRUE(IsValid(doubleBufferedDirect));
        EXPECT_TRUE(IsValid(doubleBufferedDoubleBuffered));
    }

    TEST(WorldExecutionModeConfigTests, RejectsUnknownModeValues)
    {
        constexpr WorldExecutionModeConfig invalidInbound{
            static_cast<WorldInboundMode>(2),
            WorldOutboundMode::Direct,
        };
        constexpr WorldExecutionModeConfig invalidOutbound{
            WorldInboundMode::TargetServerTick,
            static_cast<WorldOutboundMode>(2),
        };

        EXPECT_FALSE(IsValid(invalidInbound));
        EXPECT_FALSE(IsValid(invalidOutbound));
    }
} // namespace psnr::world
