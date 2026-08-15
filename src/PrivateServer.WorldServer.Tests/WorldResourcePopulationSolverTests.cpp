#include "pch.h"

#include "WorldResourcePopulationSolver.h"

#include <limits>

namespace psnr::world::tests
{
    TEST(WorldResourcePopulationSolverTests, ComputesDensityTargetAndShortageFromAllCurrentResources)
    {
        constexpr WorldActiveArea StartArea{0.0f, 0.0f, 100.0f, 1.0f, 100.0f};
        constexpr WorldActiveArea EndArea{0.0f, 0.0f, 100.0f, 0.25f, 25.0f};
        WorldResult<WorldResourcePopulationPlan> startPlanResult =
            WorldResourcePopulationSolver::Solve(StartArea, 0.05f, 0);
        WorldResult<WorldResourcePopulationPlan> endPlanResult =
            WorldResourcePopulationSolver::Solve(EndArea, 0.05f, 95);
        ASSERT_TRUE(startPlanResult.Succeeded());
        ASSERT_TRUE(endPlanResult.Succeeded());
        const WorldResourcePopulationPlan startPlan = startPlanResult.TakeValue();
        const WorldResourcePopulationPlan endPlan = endPlanResult.TakeValue();

        EXPECT_EQ(startPlan.targetResourceCount, 1571u);
        EXPECT_EQ(startPlan.shortage, 1571u);
        EXPECT_EQ(endPlan.targetResourceCount, 98u);
        EXPECT_EQ(endPlan.shortage, 3u);
    }

    TEST(WorldResourcePopulationSolverTests, TreatsTargetAsRefillMinimumInsteadOfGlobalMaximum)
    {
        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 100.0f, 0.25f, 25.0f};
        WorldResult<WorldResourcePopulationPlan> result = WorldResourcePopulationSolver::Solve(ActiveArea, 0.05f, 110);
        ASSERT_TRUE(result.Succeeded());
        const WorldResourcePopulationPlan plan = result.TakeValue();

        EXPECT_EQ(plan.targetResourceCount, 98u);
        EXPECT_EQ(plan.shortage, 0u);
    }

    TEST(WorldResourcePopulationSolverTests, RejectsInvalidOrOverflowingInput)
    {
        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 100.0f, 1.0f, 100.0f};
        constexpr WorldActiveArea InvalidArea{0.0f, 0.0f, 100.0f, 0.25f, 100.0f};
        const WorldResult<WorldResourcePopulationPlan> invalidAreaResult =
            WorldResourcePopulationSolver::Solve(InvalidArea, 0.05f, 0);
        const WorldResult<WorldResourcePopulationPlan> zeroDensityResult =
            WorldResourcePopulationSolver::Solve(ActiveArea, 0.0f, 0);
        const WorldResult<WorldResourcePopulationPlan> nanDensityResult =
            WorldResourcePopulationSolver::Solve(ActiveArea, std::numeric_limits<float>::quiet_NaN(), 0);
        ASSERT_TRUE(invalidAreaResult.Failed());
        EXPECT_EQ(invalidAreaResult.Error(), WorldErrorCode::InvalidInput);
        ASSERT_TRUE(zeroDensityResult.Failed());
        EXPECT_EQ(zeroDensityResult.Error(), WorldErrorCode::InvalidInput);
        ASSERT_TRUE(nanDensityResult.Failed());
        EXPECT_EQ(nanDensityResult.Error(), WorldErrorCode::InvalidInput);

        constexpr float Maximum = std::numeric_limits<float>::max();
        constexpr WorldActiveArea OverflowArea{0.0f, 0.0f, Maximum, 1.0f, Maximum};
        const WorldResult<WorldResourcePopulationPlan> overflowResult =
            WorldResourcePopulationSolver::Solve(OverflowArea, Maximum, 0);
        ASSERT_TRUE(overflowResult.Failed());
        EXPECT_EQ(overflowResult.Error(), WorldErrorCode::ArithmeticOverflow);
    }
} // namespace psnr::world::tests
