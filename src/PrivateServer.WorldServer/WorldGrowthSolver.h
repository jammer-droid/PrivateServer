#pragma once

#include "WorldResult.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldGrowthConfig final
    {
        float initialLength = 0.0f;
        float lengthPerPoint = 0.0f;
        float initialDiameter = 0.0f;
        float diameterPerPoint = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldGrowthConfig& left,
                                             const WorldGrowthConfig& right) noexcept = default;
    };

    struct WorldGrowthDimensions final
    {
        float nominalLength = 0.0f;
        float diameter = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldGrowthDimensions& left,
                                             const WorldGrowthDimensions& right) noexcept = default;
    };

    class WorldGrowthSolver final
    {
    public:
        [[nodiscard]] static bool IsValidConfig(const WorldGrowthConfig& config) noexcept;
        [[nodiscard]] static WorldResult<WorldGrowthDimensions> Solve(const WorldGrowthConfig& config,
                                                                      std::uint32_t growthPoint) noexcept;
    };
} // namespace psnr::world
