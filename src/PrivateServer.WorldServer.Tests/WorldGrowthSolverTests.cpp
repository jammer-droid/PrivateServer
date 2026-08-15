#include "pch.h"

#include "WorldGrowthSolver.h"

#include <limits>

namespace psnr::world::tests
{
    namespace
    {
        constexpr WorldGrowthConfig Config{
            10.0f,
            0.25f,
            1.0f,
            0.01f,
        };
    } // namespace

    TEST(WorldGrowthSolverTests, CalculatesInitialAndGrownDimensions)
    {
        WorldResult<WorldGrowthDimensions> initialResult = WorldGrowthSolver::Solve(Config, 0);
        ASSERT_TRUE(initialResult.Succeeded());
        const WorldGrowthDimensions initial = initialResult.TakeValue();
        EXPECT_FLOAT_EQ(initial.nominalLength, 10.0f);
        EXPECT_FLOAT_EQ(initial.diameter, 1.0f);

        WorldResult<WorldGrowthDimensions> grownResult = WorldGrowthSolver::Solve(Config, 10);
        ASSERT_TRUE(grownResult.Succeeded());
        const WorldGrowthDimensions grown = grownResult.TakeValue();
        EXPECT_FLOAT_EQ(grown.nominalLength, 12.5f);
        EXPECT_FLOAT_EQ(grown.diameter, 1.1f);
        EXPECT_FLOAT_EQ(grown.diameter * 0.5f, 0.55f);
    }

    TEST(WorldGrowthSolverTests, MatchesMaximumPrototypeGrowthPoint)
    {
        WorldResult<WorldGrowthDimensions> result = WorldGrowthSolver::Solve(Config, 512);
        ASSERT_TRUE(result.Succeeded());
        const WorldGrowthDimensions dimensions = result.TakeValue();

        EXPECT_FLOAT_EQ(dimensions.nominalLength, 138.0f);
        EXPECT_FLOAT_EQ(dimensions.diameter, 6.12f);
    }

    TEST(WorldGrowthSolverTests, RejectsInvalidValues)
    {
        WorldGrowthConfig invalidConfig = Config;
        invalidConfig.diameterPerPoint = 0.0f;
        WorldGrowthConfig overflowConfig = Config;
        overflowConfig.lengthPerPoint = std::numeric_limits<float>::max();

        const WorldResult<WorldGrowthDimensions> invalidConfigResult = WorldGrowthSolver::Solve(invalidConfig, 1);
        const WorldResult<WorldGrowthDimensions> overflowResult = WorldGrowthSolver::Solve(overflowConfig, 2);
        ASSERT_TRUE(invalidConfigResult.Failed());
        EXPECT_EQ(invalidConfigResult.Error(), WorldErrorCode::InvalidConfig);
        ASSERT_TRUE(overflowResult.Failed());
        EXPECT_EQ(overflowResult.Error(), WorldErrorCode::ArithmeticOverflow);
    }
} // namespace psnr::world::tests
