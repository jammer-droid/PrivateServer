#include "pch.h"

#include "WorldControlMovementSolver.h"

#include <limits>
#include <numbers>

namespace psnr::world::tests
{
    namespace
    {
        constexpr WorldControlMovementConfig Config{
            5.0f,
            7.5f,
            std::numbers::pi_v<float>,
        };
    } // namespace

    TEST(WorldControlMovementSolverTests, AppliesTurnAndBaseSpeed)
    {
        WorldResult<WorldControlMovementOutput> leftResult = WorldControlMovementSolver::Solve(
            Config, WorldControlMovementInput{0.0f, WorldTurnState::Left, false}, 0.5f);
        WorldResult<WorldControlMovementOutput> straightResult = WorldControlMovementSolver::Solve(
            Config, WorldControlMovementInput{0.0f, WorldTurnState::Straight, false}, 0.5f);
        WorldResult<WorldControlMovementOutput> rightResult = WorldControlMovementSolver::Solve(
            Config, WorldControlMovementInput{0.0f, WorldTurnState::Right, false}, 0.5f);
        ASSERT_TRUE(leftResult.Succeeded());
        ASSERT_TRUE(straightResult.Succeeded());
        ASSERT_TRUE(rightResult.Succeeded());
        const WorldControlMovementOutput left = leftResult.TakeValue();
        const WorldControlMovementOutput straight = straightResult.TakeValue();
        const WorldControlMovementOutput right = rightResult.TakeValue();

        EXPECT_NEAR(left.headingRadians, std::numbers::pi_v<float> / 2.0f, 0.000001f);
        EXPECT_NEAR(left.velocityX, 0.0f, 0.000001f);
        EXPECT_NEAR(left.velocityY, 5.0f, 0.000001f);
        EXPECT_NEAR(straight.headingRadians, 0.0f, 0.000001f);
        EXPECT_NEAR(straight.velocityX, 5.0f, 0.000001f);
        EXPECT_NEAR(straight.velocityY, 0.0f, 0.000001f);
        EXPECT_NEAR(right.headingRadians, -std::numbers::pi_v<float> / 2.0f, 0.000001f);
        EXPECT_NEAR(right.velocityX, 0.0f, 0.000001f);
        EXPECT_NEAR(right.velocityY, -5.0f, 0.000001f);
    }

    TEST(WorldControlMovementSolverTests, UsesBoostSpeedAndNormalizesHeading)
    {
        WorldResult<WorldControlMovementOutput> result = WorldControlMovementSolver::Solve(
            Config, WorldControlMovementInput{std::numbers::pi_v<float> * 0.75f, WorldTurnState::Right, true}, 0.5f);
        ASSERT_TRUE(result.Succeeded());
        const WorldControlMovementOutput movement = result.TakeValue();

        EXPECT_NEAR(movement.headingRadians, std::numbers::pi_v<float> * 0.25f, 0.000001f);
        EXPECT_NEAR(movement.speed, 7.5f, 0.000001f);
        EXPECT_NEAR(movement.directionX * movement.directionX + movement.directionY * movement.directionY, 1.0f,
                    0.000001f);
        EXPECT_NEAR(movement.velocityX, movement.directionX * 7.5f, 0.000001f);
        EXPECT_NEAR(movement.velocityY, movement.directionY * 7.5f, 0.000001f);
    }

    TEST(WorldControlMovementSolverTests, RejectsInvalidValues)
    {
        WorldControlMovementConfig invalidConfig = Config;
        invalidConfig.boostSpeed = 4.0f;

        const WorldResult<WorldControlMovementOutput> invalidConfigResult =
            WorldControlMovementSolver::Solve(invalidConfig, WorldControlMovementInput{}, 0.05f);
        const WorldResult<WorldControlMovementOutput> invalidInputResult = WorldControlMovementSolver::Solve(
            Config, WorldControlMovementInput{std::numeric_limits<float>::infinity(), WorldTurnState::Straight, false},
            0.05f);
        ASSERT_TRUE(invalidConfigResult.Failed());
        EXPECT_EQ(invalidConfigResult.Error(), WorldErrorCode::InvalidConfig);
        ASSERT_TRUE(invalidInputResult.Failed());
        EXPECT_EQ(invalidInputResult.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world::tests
