#pragma once

#include "WorldGrowthSolver.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldBoostCostConfig final
    {
        WorldGrowthConfig growth{};
        float initialLengthCostPerSecond = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldBoostCostConfig& left,
                                             const WorldBoostCostConfig& right) noexcept = default;
    };

    struct WorldBoostCostState final
    {
        std::uint32_t growthPoint = 0;
        float boostCostAccumulator = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldBoostCostState& left,
                                             const WorldBoostCostState& right) noexcept = default;
    };

    class WorldBoostCostSolver final
    {
    public:
        [[nodiscard]] static bool IsDisabled(const WorldBoostCostConfig& config) noexcept;
        [[nodiscard]] static bool IsValidConfig(const WorldBoostCostConfig& config) noexcept;
        [[nodiscard]] static WorldResult<WorldBoostCostState> Solve(const WorldBoostCostConfig& config,
                                                                    const WorldBoostCostState& currentState,
                                                                    bool boostRequested,
                                                                    float fixedDeltaSeconds) noexcept;
    };
} // namespace psnr::world
