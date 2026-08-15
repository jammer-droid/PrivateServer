#pragma once

#include "WorldGameplayState.h"

#include <cstdint>
#include <vector>

namespace psnr::world
{
    struct WorldRoundResultPlayerInput final
    {
        std::uint32_t playerId = 0;
        std::uint32_t growthPoint = 0;
        WorldPlayerLifecycle lifecycle = WorldPlayerLifecycle::Invalid;
        bool activeSessionConnected = false;
    };

    struct WorldRoundResultPlan final
    {
        std::uint32_t winningGrowthPoint = 0;
        std::vector<std::uint32_t> winnerPlayerIds;
    };

    class WorldRoundResultPlanner final
    {
    public:
        [[nodiscard]] WorldResult<WorldRoundResultPlan> Plan(
            const std::vector<WorldRoundResultPlayerInput>& players) const noexcept;
    };
} // namespace psnr::world
