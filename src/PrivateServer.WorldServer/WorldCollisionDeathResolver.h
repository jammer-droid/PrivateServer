#pragma once

#include "WorldCollisionProxyBatch.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    // 한 tick의 player collision contact를 동시에 commit할 stable death set으로 해석한다.
    class WorldCollisionDeathResolver final
    {
    public:
        [[nodiscard]] static WorldResult<std::vector<WorldEntityKey>> Resolve(
            std::span<const WorldCollisionContact> contacts) noexcept;
    };
} // namespace psnr::world
