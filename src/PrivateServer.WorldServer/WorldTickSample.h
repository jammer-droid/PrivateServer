#pragma once

#include "WorldGameplayState.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldTickSample final
    {
        std::uint64_t epoch = 0;
        std::uint32_t firstServerTick = 0;
        std::uint32_t lastServerTick = 0;
        std::uint32_t roundId = 0;
        WorldRoundPhase roundPhase = WorldRoundPhase::Invalid;
        std::uint32_t processedTickCount = 0;
        std::uint64_t dueTickCount = 0;
        std::uint64_t startLagNanoseconds = 0;
        std::uint64_t executionDurationNanoseconds = 0;

        [[nodiscard]] friend bool operator==(const WorldTickSample& left,
                                             const WorldTickSample& right) noexcept = default;
    };
} // namespace psnr::world
