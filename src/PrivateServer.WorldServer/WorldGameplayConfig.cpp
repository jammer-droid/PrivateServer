#include "pch.h"

#include "WorldGameplayConfig.h"

#include "WorldActiveAreaSolver.h"
#include "WorldResourcePopulationSolver.h"

#include <cmath>

namespace psnr::world
{
    bool IsValid(const WorldGameplayConfig& config, const WorldPhysicsArenaBounds& arenaBounds) noexcept
    {
        const WorldActiveAreaConfig activeAreaConfig{
            arenaBounds,
            config.roundDurationTicks,
            config.activeAreaStartRatio,
            config.activeAreaEndRatio,
        };
        if (!WorldPhysicsArenaBounds::IsValid(arenaBounds) || config.minimumPlayersToStart == 0 ||
            config.scoreToWin == 0 || config.roundDurationTicks == 0 || config.endedDurationTicks == 0 ||
            config.resourceArchetypeId == 0 || !std::isfinite(config.resourceCircleRadius) ||
            config.resourceCircleRadius <= 0.0f || config.resourceScoreValue == 0 ||
            !std::isfinite(config.resourceDensityPerUnit2) || config.resourceDensityPerUnit2 <= 0.0f ||
            !WorldActiveAreaSolver::IsValidConfig(activeAreaConfig) ||
            (!WorldBoostCostSolver::IsDisabled(config.boostCost) &&
             !WorldBoostCostSolver::IsValidConfig(config.boostCost)))
        {
            return false;
        }

        WorldResult<WorldActiveArea> smallestActiveAreaResult =
            WorldActiveAreaSolver::Solve(activeAreaConfig, 0, config.roundDurationTicks);
        WorldResult<WorldActiveArea> startActiveAreaResult = WorldActiveAreaSolver::Solve(activeAreaConfig, 0, 0);
        if (smallestActiveAreaResult.Failed() || startActiveAreaResult.Failed())
        {
            return false;
        }
        const WorldActiveArea smallestActiveArea = smallestActiveAreaResult.TakeValue();
        const WorldActiveArea startActiveArea = startActiveAreaResult.TakeValue();
        WorldResult<WorldResourcePopulationPlan> startPopulationResult =
            WorldResourcePopulationSolver::Solve(startActiveArea, config.resourceDensityPerUnit2, 0);
        if (config.resourceCircleRadius >= smallestActiveArea.radius || startPopulationResult.Failed() ||
            startPopulationResult.Value().targetResourceCount == 0)
        {
            return false;
        }

        return true;
    }
} // namespace psnr::world
