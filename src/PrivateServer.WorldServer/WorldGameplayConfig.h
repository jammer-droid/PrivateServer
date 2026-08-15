#pragma once

#include "WorldBoostCostSolver.h"
#include "WorldPhysicsValues.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldGameplayConfig final
    {
        std::uint32_t minimumPlayersToStart = 0;
        std::uint32_t scoreToWin = 0;
        std::uint32_t roundDurationTicks = 0;
        std::uint32_t endedDurationTicks = 0;
        std::uint32_t resourceArchetypeId = 0;
        float resourceCircleRadius = 0.0f;
        std::uint32_t resourceScoreValue = 0;
        float resourceDensityPerUnit2 = 0.0f;
        WorldBoostCostConfig boostCost{};
        float activeAreaStartRatio = 1.0f;
        float activeAreaEndRatio = 1.0f;

        [[nodiscard]] friend bool operator==(const WorldGameplayConfig& left,
                                             const WorldGameplayConfig& right) noexcept = default;
    };

    [[nodiscard]] bool IsValid(const WorldGameplayConfig& config, const WorldPhysicsArenaBounds& arenaBounds) noexcept;
} // namespace psnr::world
