#include "pch.h"

#include "WorldBoostCostSolver.h"

#include <limits>

namespace psnr::world::tests
{
    namespace
    {
        constexpr WorldBoostCostConfig Config{
            WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f},
            4.0f,
        };
    } // namespace

    TEST(WorldBoostCostSolverTests, ClassifiesOnlyTheCanonicalZeroConfigAsDisabled)
    {
        EXPECT_TRUE(WorldBoostCostSolver::IsDisabled(WorldBoostCostConfig{}));

        WorldBoostCostConfig partiallyConfigured = WorldBoostCostConfig{};
        partiallyConfigured.initialLengthCostPerSecond = 1.0f;
        EXPECT_FALSE(WorldBoostCostSolver::IsDisabled(partiallyConfigured));
        EXPECT_FALSE(WorldBoostCostSolver::IsDisabled(Config));
    }

    TEST(WorldBoostCostSolverTests, AccumulatesFractionalCostAndSpendsWholePoints)
    {
        WorldResult<WorldBoostCostState> firstResult =
            WorldBoostCostSolver::Solve(Config, WorldBoostCostState{10, 0.25f}, true, 0.1f);
        ASSERT_TRUE(firstResult.Succeeded());
        const WorldBoostCostState first = firstResult.TakeValue();
        EXPECT_EQ(first.growthPoint, 10u);
        EXPECT_NEAR(first.boostCostAccumulator, 0.75f, 0.000001f);

        WorldResult<WorldBoostCostState> secondResult = WorldBoostCostSolver::Solve(Config, first, true, 0.1f);
        ASSERT_TRUE(secondResult.Succeeded());
        const WorldBoostCostState second = secondResult.TakeValue();
        EXPECT_EQ(second.growthPoint, 9u);
        EXPECT_NEAR(second.boostCostAccumulator, 0.25f, 0.000001f);
        EXPECT_EQ(first.growthPoint - second.growthPoint, 1u);

        WorldResult<WorldBoostCostState> thirdResult = WorldBoostCostSolver::Solve(Config, second, true, 0.1f);
        ASSERT_TRUE(thirdResult.Succeeded());
        const WorldBoostCostState third = thirdResult.TakeValue();
        EXPECT_EQ(third.growthPoint, 9u);
        EXPECT_NEAR(third.boostCostAccumulator, 0.74f, 0.000001f);
    }

    TEST(WorldBoostCostSolverTests, ExhaustsLastPoint)
    {
        WorldResult<WorldBoostCostState> result =
            WorldBoostCostSolver::Solve(Config, WorldBoostCostState{1, 0.9f}, true, 0.1f);
        ASSERT_TRUE(result.Succeeded());
        const WorldBoostCostState output = result.TakeValue();

        EXPECT_EQ(output.growthPoint, 0u);
        EXPECT_NEAR(output.boostCostAccumulator, 0.31f, 0.000001f);
    }

    TEST(WorldBoostCostSolverTests, PreservesAccumulatorWhileBoostIsInactive)
    {
        WorldResult<WorldBoostCostState> releasedResult =
            WorldBoostCostSolver::Solve(Config, WorldBoostCostState{3, 0.75f}, false, 0.1f);
        ASSERT_TRUE(releasedResult.Succeeded());
        const WorldBoostCostState released = releasedResult.TakeValue();
        EXPECT_EQ(released, (WorldBoostCostState{3, 0.75f}));

        WorldResult<WorldBoostCostState> emptyResult =
            WorldBoostCostSolver::Solve(Config, WorldBoostCostState{0, 1.25f}, true, 0.1f);
        ASSERT_TRUE(emptyResult.Succeeded());
        const WorldBoostCostState empty = emptyResult.TakeValue();
        EXPECT_EQ(empty, (WorldBoostCostState{0, 1.25f}));
    }

    TEST(WorldBoostCostSolverTests, RejectsInvalidValues)
    {
        WorldBoostCostConfig invalidConfig = Config;
        invalidConfig.growth.initialLength = 0.0f;

        const WorldResult<WorldBoostCostState> invalidConfigResult =
            WorldBoostCostSolver::Solve(invalidConfig, WorldBoostCostState{}, false, 0.1f);
        const WorldResult<WorldBoostCostState> invalidInputResult = WorldBoostCostSolver::Solve(
            Config, WorldBoostCostState{1, std::numeric_limits<float>::infinity()}, true, 0.1f);
        ASSERT_TRUE(invalidConfigResult.Failed());
        EXPECT_EQ(invalidConfigResult.Error(), WorldErrorCode::InvalidConfig);
        ASSERT_TRUE(invalidInputResult.Failed());
        EXPECT_EQ(invalidInputResult.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world::tests
