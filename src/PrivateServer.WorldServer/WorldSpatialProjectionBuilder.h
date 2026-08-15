#pragma once

#include "WorldEntityManager.h"
#include "WorldResult.h"
#include "WorldSpatialProxy.h"

#include <cstdint>
#include <vector>

namespace psnr::world
{
    class WorldSpatialProjectionBuilder final
    {
    public:
        [[nodiscard]] static WorldResult<std::vector<WorldSpatialProxy>> Build(
            const WorldEntityManager& entityManager) noexcept;
    };
} // namespace psnr::world
