#include "pch.h"

#include "WorldGameplayConfig.h"

#include <limits>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldPhysicsArenaBounds MakeArenaBounds() noexcept
        {
            return WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f};
        }

        [[nodiscard]] WorldGameplayConfig MakeValidConfig()
        {
            WorldGameplayConfig config;
            config.minimumPlayersToStart = 2;
            config.scoreToWin = 5;
            config.roundDurationTicks = 1200;
            config.endedDurationTicks = 60;
            config.resourceArchetypeId = 2;
            config.resourceCircleRadius = 0.5f;
            config.resourceScoreValue = 1;
            config.resourceDensityPerUnit2 = 0.000127324f;
            config.boostCost = WorldBoostCostConfig{WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f}, 4.0f};
            return config;
        }
    } // namespace

    TEST(WorldGameplayConfigTests, AcceptsAmbientResourceDensityBaseline)
    {
        const WorldGameplayConfig config = MakeValidConfig();

        EXPECT_TRUE(IsValid(config, MakeArenaBounds()));
    }

    TEST(WorldGameplayConfigTests, RejectsZeroRuleValuesAndInvalidDensity)
    {
        WorldGameplayConfig config = MakeValidConfig();

        config.minimumPlayersToStart = 0;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.scoreToWin = 0;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.roundDurationTicks = 0;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.endedDurationTicks = 0;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.resourceArchetypeId = 0;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.resourceCircleRadius = 0.0f;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.resourceScoreValue = 0;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.resourceDensityPerUnit2 = 0.0f;
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
        config = MakeValidConfig();
        config.resourceDensityPerUnit2 = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
    }

    TEST(WorldGameplayConfigTests, RejectsDensityTargetThatRoundsToZeroOrOverflows)
    {
        WorldGameplayConfig config = MakeValidConfig();
        config.resourceDensityPerUnit2 = std::numeric_limits<float>::denorm_min();
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));

        config = MakeValidConfig();
        config.resourceDensityPerUnit2 = std::numeric_limits<float>::max();
        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
    }

    TEST(WorldGameplayConfigTests, RejectsResourceCircleThatCannotFitSmallestActiveArea)
    {
        WorldGameplayConfig config = MakeValidConfig();
        config.activeAreaEndRatio = 0.25f;
        config.resourceCircleRadius = 25.0f;

        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
    }

    TEST(WorldGameplayConfigTests, RejectsPartiallyConfiguredBoostCost)
    {
        WorldGameplayConfig config = MakeValidConfig();
        config.boostCost.growth.lengthPerPoint = 0.0f;

        EXPECT_FALSE(IsValid(config, MakeArenaBounds()));
    }
} // namespace psnr::world::tests
