#include "pch.h"

#include "WorldActiveAreaSolver.h"

#include <limits>

namespace psnr::world::tests
{
    namespace
    {
        constexpr WorldActiveAreaConfig Config{
            WorldPhysicsArenaBounds{-100.0f, -80.0f, 100.0f, 80.0f},
            100,
            1.0f,
            0.25f,
        };
    } // namespace

    TEST(WorldActiveAreaSolverTests, InterpolatesAndClampsRadiusAtRoundTicks)
    {
        WorldResult<WorldActiveArea> beforeStartResult = WorldActiveAreaSolver::Solve(Config, 100, 90);
        WorldResult<WorldActiveArea> atStartResult = WorldActiveAreaSolver::Solve(Config, 100, 100);
        WorldResult<WorldActiveArea> middleResult = WorldActiveAreaSolver::Solve(Config, 100, 150);
        WorldResult<WorldActiveArea> atEndResult = WorldActiveAreaSolver::Solve(Config, 100, 200);
        WorldResult<WorldActiveArea> afterEndResult = WorldActiveAreaSolver::Solve(Config, 100, 250);
        ASSERT_TRUE(beforeStartResult.Succeeded());
        ASSERT_TRUE(atStartResult.Succeeded());
        ASSERT_TRUE(middleResult.Succeeded());
        ASSERT_TRUE(atEndResult.Succeeded());
        ASSERT_TRUE(afterEndResult.Succeeded());
        const WorldActiveArea beforeStart = beforeStartResult.TakeValue();
        const WorldActiveArea atStart = atStartResult.TakeValue();
        const WorldActiveArea middle = middleResult.TakeValue();
        const WorldActiveArea atEnd = atEndResult.TakeValue();
        const WorldActiveArea afterEnd = afterEndResult.TakeValue();

        EXPECT_FLOAT_EQ(beforeStart.centerX, 0.0f);
        EXPECT_FLOAT_EQ(beforeStart.centerY, 0.0f);
        EXPECT_FLOAT_EQ(beforeStart.referenceRadius, 80.0f);
        EXPECT_FLOAT_EQ(beforeStart.ratio, 1.0f);
        EXPECT_FLOAT_EQ(beforeStart.radius, 80.0f);
        EXPECT_EQ(atStart, beforeStart);
        EXPECT_FLOAT_EQ(middle.ratio, 0.625f);
        EXPECT_FLOAT_EQ(middle.radius, 50.0f);
        EXPECT_FLOAT_EQ(atEnd.ratio, 0.25f);
        EXPECT_FLOAT_EQ(atEnd.radius, 20.0f);
        EXPECT_EQ(afterEnd, atEnd);
    }

    TEST(WorldActiveAreaSolverTests, ContainsOnlyCirclesStrictlyInsideCurrentArea)
    {
        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 10.0f, 1.0f, 10.0f};

        EXPECT_TRUE(ActiveArea.ContainsCircleStrictly(9.49f, 0.0f, 0.5f));
        EXPECT_FALSE(ActiveArea.ContainsCircleStrictly(9.5f, 0.0f, 0.5f));
        EXPECT_FALSE(ActiveArea.ContainsCircleStrictly(10.0f, 0.0f, 0.5f));
    }

    TEST(WorldActiveAreaSolverTests, RejectsInvalidValues)
    {
        WorldActiveAreaConfig invalidConfig = Config;
        invalidConfig.endRatio = 0.0f;

        const WorldResult<WorldActiveArea> invalidConfigResult = WorldActiveAreaSolver::Solve(invalidConfig, 100, 100);
        const WorldResult<WorldActiveArea> invalidInputResult = WorldActiveAreaSolver::Solve(
            Config, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max());
        ASSERT_TRUE(invalidConfigResult.Failed());
        EXPECT_EQ(invalidConfigResult.Error(), WorldErrorCode::InvalidConfig);
        ASSERT_TRUE(invalidInputResult.Failed());
        EXPECT_EQ(invalidInputResult.Error(), WorldErrorCode::InvalidInput);

        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 10.0f, 1.0f, 10.0f};
        constexpr WorldActiveArea ContradictoryArea{0.0f, 0.0f, 10.0f, 0.25f, 10.0f};

        EXPECT_FALSE(ContradictoryArea.ContainsCircleStrictly(0.0f, 0.0f, 0.5f));
        EXPECT_FALSE(ActiveArea.ContainsCircleStrictly(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.5f));
        EXPECT_FALSE(ActiveArea.ContainsCircleStrictly(0.0f, 0.0f, 0.0f));
    }
} // namespace psnr::world::tests
