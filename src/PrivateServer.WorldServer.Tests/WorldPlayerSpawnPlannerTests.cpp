#include "pch.h"

#include "WorldPlayerSpawnPlanner.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] constexpr WorldPlayerSpawnPlannerConfig MakeConfig() noexcept
        {
            return WorldPlayerSpawnPlannerConfig{
                WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
                4,
                1,
                5.0f,
                WorldPlayerBodyConfig{WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f}, 16},
            };
        }
    } // namespace

    TEST(WorldPlayerSpawnPlannerTests, ProducesSameCandidateForSameTickPlayerAndOrdinal)
    {
        constexpr WorldPlayerSpawnPlannerConfig Config = MakeConfig();
        WorldResult<WorldPlayerSpawnCandidate> firstResult = WorldPlayerSpawnPlanner::Plan(Config, 100, 7, 0);
        WorldResult<WorldPlayerSpawnCandidate> secondResult = WorldPlayerSpawnPlanner::Plan(Config, 100, 7, 0);
        ASSERT_TRUE(firstResult.Succeeded());
        ASSERT_TRUE(secondResult.Succeeded());
        const WorldPlayerSpawnCandidate first = firstResult.TakeValue();
        const WorldPlayerSpawnCandidate second = secondResult.TakeValue();

        EXPECT_EQ(first.ordinal, 0u);
        EXPECT_EQ(first.playerId, 7u);
        EXPECT_EQ(first.playerId, second.playerId);
        EXPECT_EQ(first.ordinal, second.ordinal);
        EXPECT_EQ(first.components, second.components);
        EXPECT_EQ(first.bounds, second.bounds);
        EXPECT_GT(first.bounds.minX, Config.arenaBounds.minimumX);
        EXPECT_GT(first.bounds.minY, Config.arenaBounds.minimumY);
        EXPECT_LT(first.bounds.maxX, Config.arenaBounds.maximumX);
        EXPECT_LT(first.bounds.maxY, Config.arenaBounds.maximumY);
    }

    TEST(WorldPlayerSpawnPlannerTests, ProducesDifferentCandidatesForDifferentOrdinals)
    {
        constexpr WorldPlayerSpawnPlannerConfig Config = MakeConfig();
        WorldResult<WorldPlayerSpawnCandidate> firstResult = WorldPlayerSpawnPlanner::Plan(Config, 100, 7, 0);
        WorldResult<WorldPlayerSpawnCandidate> secondResult = WorldPlayerSpawnPlanner::Plan(Config, 100, 7, 1);
        ASSERT_TRUE(firstResult.Succeeded());
        ASSERT_TRUE(secondResult.Succeeded());
        const WorldPlayerSpawnCandidate first = firstResult.TakeValue();
        const WorldPlayerSpawnCandidate second = secondResult.TakeValue();

        EXPECT_EQ(first.ordinal, 0u);
        EXPECT_EQ(first.playerId, 7u);
        EXPECT_EQ(second.ordinal, 1u);
        EXPECT_EQ(second.playerId, 7u);
        EXPECT_NE(first.components.transform, second.components.transform);
        EXPECT_NE(first.bounds, second.bounds);
    }

    TEST(WorldPlayerSpawnPlannerTests, RejectsOutOfRangeOrdinal)
    {
        constexpr WorldPlayerSpawnPlannerConfig Config = MakeConfig();
        const WorldResult<WorldPlayerSpawnCandidate> result =
            WorldPlayerSpawnPlanner::Plan(Config, 100, 7, Config.maxCandidatesPerTick);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidArgument);
    }
} // namespace psnr::world::tests
