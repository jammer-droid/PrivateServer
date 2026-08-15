#include "pch.h"

#include "WorldRoundResultPlanner.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace psnr::world
{
    WorldResult<WorldRoundResultPlan> WorldRoundResultPlanner::Plan(
        const std::vector<WorldRoundResultPlayerInput>& players) const noexcept
    {
        try
        {
            std::vector<std::size_t> playerOrder(players.size());
            for (std::size_t index = 0; index < playerOrder.size(); ++index)
            {
                const WorldRoundResultPlayerInput& player = players[index];
                if (player.playerId == 0 || (player.lifecycle != WorldPlayerLifecycle::Alive &&
                                             player.lifecycle != WorldPlayerLifecycle::SpawnPending))
                {
                    return WorldResult<WorldRoundResultPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                playerOrder[index] = index;
            }
            std::sort(playerOrder.begin(), playerOrder.end(),
                      [&players](const std::size_t left, const std::size_t right)
                      { return players[left].playerId < players[right].playerId; });

            WorldRoundResultPlan plan;
            bool hasWinner = false;
            for (std::size_t orderIndex = 0; orderIndex < playerOrder.size(); ++orderIndex)
            {
                const WorldRoundResultPlayerInput& player = players[playerOrder[orderIndex]];
                if (orderIndex > 0 && players[playerOrder[orderIndex - 1]].playerId == player.playerId)
                {
                    return WorldResult<WorldRoundResultPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                if (player.lifecycle != WorldPlayerLifecycle::Alive || !player.activeSessionConnected)
                {
                    continue;
                }

                if (!hasWinner || player.growthPoint > plan.winningGrowthPoint)
                {
                    hasWinner = true;
                    plan.winningGrowthPoint = player.growthPoint;
                    plan.winnerPlayerIds.clear();
                    plan.winnerPlayerIds.push_back(player.playerId);
                }
                else if (player.growthPoint == plan.winningGrowthPoint)
                {
                    // playerOrder is ascending, so co-winners are already in stable player ID order.
                    plan.winnerPlayerIds.push_back(player.playerId);
                }
            }

            return WorldResult<WorldRoundResultPlan>(std::move(plan));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldRoundResultPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
