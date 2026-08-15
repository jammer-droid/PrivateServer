#pragma once

#include <cstdint>

namespace psnr::world
{
    struct WorldReplicationConfig final
    {
        std::uint32_t snapshotIntervalTicks = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return snapshotIntervalTicks > 0;
        }

        [[nodiscard]] bool IsSnapshotTick(const std::uint64_t serverTick) const noexcept
        {
            return IsValid() && serverTick % snapshotIntervalTicks == 0;
        }
    };
} // namespace psnr::world
