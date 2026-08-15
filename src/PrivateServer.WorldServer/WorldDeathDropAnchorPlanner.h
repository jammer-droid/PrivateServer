#pragma once

#include "WorldBodyTrailComponent.h"

#include <cstdint>
#include <vector>

namespace psnr::world
{
    struct WorldDeathDropAnchor final
    {
        std::uint32_t dropOrdinal = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldDeathDropAnchor& left,
                                             const WorldDeathDropAnchor& right) noexcept = default;
    };

    class WorldDeathDropAnchorPlanner final
    {
    public:
        [[nodiscard]] static WorldResult<std::vector<WorldDeathDropAnchor>> Plan(float headPositionX,
                                                                                 float headPositionY,
                                                                                 const BodyTrailComponent& bodyTrail,
                                                                                 std::uint32_t growthPoint) noexcept;
    };
} // namespace psnr::world
