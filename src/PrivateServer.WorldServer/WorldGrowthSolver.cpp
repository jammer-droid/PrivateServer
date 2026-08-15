#include "pch.h"

#include "WorldGrowthSolver.h"

#include <cmath>

namespace psnr::world
{
    bool WorldGrowthSolver::IsValidConfig(const WorldGrowthConfig& config) noexcept
    {
        return std::isfinite(config.initialLength) && config.initialLength > 0.0f &&
               std::isfinite(config.lengthPerPoint) && config.lengthPerPoint > 0.0f &&
               std::isfinite(config.initialDiameter) && config.initialDiameter > 0.0f &&
               std::isfinite(config.diameterPerPoint) && config.diameterPerPoint > 0.0f;
    }

    WorldResult<WorldGrowthDimensions> WorldGrowthSolver::Solve(const WorldGrowthConfig& config,
                                                                const std::uint32_t growthPoint) noexcept
    {
        if (!IsValidConfig(config))
        {
            return WorldResult<WorldGrowthDimensions>::Failure(WorldErrorCode::InvalidConfig);
        }

        const float point = static_cast<float>(growthPoint);
        const float nominalLength = config.initialLength + point * config.lengthPerPoint;
        const float diameter = config.initialDiameter + point * config.diameterPerPoint;
        if (!std::isfinite(nominalLength) || !std::isfinite(diameter))
        {
            return WorldResult<WorldGrowthDimensions>::Failure(WorldErrorCode::ArithmeticOverflow);
        }

        return WorldResult<WorldGrowthDimensions>(WorldGrowthDimensions{nominalLength, diameter});
    }
} // namespace psnr::world
