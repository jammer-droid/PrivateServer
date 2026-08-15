#include "pch.h"

#include "WorldResourcePopulationSolver.h"

#include <cmath>
#include <limits>
#include <numbers>

namespace psnr::world
{
    WorldResult<WorldResourcePopulationPlan> WorldResourcePopulationSolver::Solve(
        const WorldActiveArea& activeArea, const float resourceDensityPerUnit2,
        const std::size_t currentResourceCount) noexcept
    {
        if (!activeArea.IsValid() || !std::isfinite(resourceDensityPerUnit2) || resourceDensityPerUnit2 <= 0.0f)
        {
            return WorldResult<WorldResourcePopulationPlan>::Failure(WorldErrorCode::InvalidInput);
        }

        // resource count = floor(pi * rad^2 * density + 0.5)
        const double radius = static_cast<double>(activeArea.radius);
        const double density = static_cast<double>(resourceDensityPerUnit2);
        const double exactTarget = std::numbers::pi_v<double> * radius * radius * density;
        const double roundedTarget = std::floor(exactTarget + 0.5);
        if (!std::isfinite(exactTarget) || !std::isfinite(roundedTarget) ||
            roundedTarget > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        {
            return WorldResult<WorldResourcePopulationPlan>::Failure(WorldErrorCode::ArithmeticOverflow);
        }

        const std::uint32_t targetResourceCount = static_cast<std::uint32_t>(roundedTarget);
        std::uint32_t shortage = 0;
        if (currentResourceCount < targetResourceCount)
        {
            shortage = targetResourceCount - static_cast<std::uint32_t>(currentResourceCount);
        }

        return WorldResult<WorldResourcePopulationPlan>(WorldResourcePopulationPlan{targetResourceCount, shortage});
    }
} // namespace psnr::world
