#pragma once

#include "WorldActiveAreaSolver.h"
#include "WorldDeathDropAnchorPlanner.h"
#include "WorldResourceRegistry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    struct WorldDeathDropPlacementPlan final
    {
        std::vector<WorldDeathDropAnchor> acceptedAnchors;
        std::size_t rejectedCount = 0;

        [[nodiscard]] friend bool operator==(const WorldDeathDropPlacementPlan& left,
                                             const WorldDeathDropPlacementPlan& right) noexcept = default;
    };

    class WorldDeathDropPlacementFilter final
    {
    public:
        [[nodiscard]] static WorldResult<WorldDeathDropPlacementPlan> Filter(
            const WorldActiveArea& activeArea, float resourceCircleRadius,
            std::span<const WorldDeathDropAnchor> candidates, const WorldResourceRegistry& resourceRegistry,
            std::span<const WorldResourcePosition> reservedPositions) noexcept;
    };
} // namespace psnr::world
