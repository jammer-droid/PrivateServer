#pragma once

#include "WorldActiveAreaSolver.h"

#include <cstddef>
#include <cstdint>

namespace psnr::world
{
    struct WorldResourcePopulationPlan final
    {
        std::uint32_t targetResourceCount = 0;
        std::uint32_t shortage = 0;

        [[nodiscard]] friend bool operator==(const WorldResourcePopulationPlan& left,
                                             const WorldResourcePopulationPlan& right) noexcept = default;
    };

    class WorldResourcePopulationSolver final
    {
    public:
        [[nodiscard]] static WorldResult<WorldResourcePopulationPlan> Solve(const WorldActiveArea& activeArea,
                                                                            float resourceDensityPerUnit2,
                                                                            std::size_t currentResourceCount) noexcept;
    };
} // namespace psnr::world
