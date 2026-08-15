#include "pch.h"

#include "WorldRoundResultPlanner.h"

#include <cstdint>
#include <vector>

namespace psnr::world::tests
{
    TEST(WorldRoundResultPlannerTests, SelectsAllHighestGrowthPlayersInStablePlayerIdOrder)
    {
        const std::vector<WorldRoundResultPlayerInput> players{
            WorldRoundResultPlayerInput{30, 12, WorldPlayerLifecycle::Alive, true},
            WorldRoundResultPlayerInput{20, 5, WorldPlayerLifecycle::Alive, true},
            WorldRoundResultPlayerInput{10, 12, WorldPlayerLifecycle::Alive, true},
        };
        const WorldRoundResultPlanner planner;
        WorldResult<WorldRoundResultPlan> result = planner.Plan(players);
        ASSERT_TRUE(result.Succeeded());
        const WorldRoundResultPlan plan = result.TakeValue();
        EXPECT_EQ(plan.winningGrowthPoint, 12u);
        EXPECT_EQ(plan.winnerPlayerIds, (std::vector<std::uint32_t>{10, 30}));
    }

    TEST(WorldRoundResultPlannerTests, ExcludesSpawnPendingAndDisconnectedPlayers)
    {
        const std::vector<WorldRoundResultPlayerInput> players{
            WorldRoundResultPlayerInput{10, 100, WorldPlayerLifecycle::SpawnPending, true},
            WorldRoundResultPlayerInput{20, 90, WorldPlayerLifecycle::Alive, false},
            WorldRoundResultPlayerInput{30, 8, WorldPlayerLifecycle::Alive, true},
        };
        const WorldRoundResultPlanner planner;
        WorldResult<WorldRoundResultPlan> result = planner.Plan(players);
        ASSERT_TRUE(result.Succeeded());
        const WorldRoundResultPlan plan = result.TakeValue();
        EXPECT_EQ(plan.winningGrowthPoint, 8u);
        EXPECT_EQ(plan.winnerPlayerIds, (std::vector<std::uint32_t>{30}));
    }

    TEST(WorldRoundResultPlannerTests, ProducesEmptyWinnerSetWhenNoPlayerIsEligible)
    {
        const std::vector<WorldRoundResultPlayerInput> players{
            WorldRoundResultPlayerInput{10, 20, WorldPlayerLifecycle::SpawnPending, true},
            WorldRoundResultPlayerInput{20, 10, WorldPlayerLifecycle::Alive, false},
        };
        const WorldRoundResultPlanner planner;
        WorldResult<WorldRoundResultPlan> result = planner.Plan(players);
        ASSERT_TRUE(result.Succeeded());
        const WorldRoundResultPlan plan = result.TakeValue();
        EXPECT_EQ(plan.winningGrowthPoint, 0u);
        EXPECT_TRUE(plan.winnerPlayerIds.empty());
    }

    TEST(WorldRoundResultPlannerTests, RejectsDuplicatePlayer)
    {
        const std::vector<WorldRoundResultPlayerInput> players{
            WorldRoundResultPlayerInput{10, 20, WorldPlayerLifecycle::Alive, true},
            WorldRoundResultPlayerInput{10, 10, WorldPlayerLifecycle::Alive, true},
        };
        const WorldRoundResultPlanner planner;
        const WorldResult<WorldRoundResultPlan> result = planner.Plan(players);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world::tests
