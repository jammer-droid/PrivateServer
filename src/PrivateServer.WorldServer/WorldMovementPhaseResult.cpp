#include "pch.h"

#include "WorldMovementPhaseResult.h"

#include <algorithm>

namespace psnr::world
{
    WorldMovementPhaseResult::WorldMovementPhaseResult(std::vector<WorldMovementEntityUpdate> updates)
        : updates_(std::move(updates))
    {
        std::sort(updates_.begin(), updates_.end(),
                  [](const WorldMovementEntityUpdate& left, const WorldMovementEntityUpdate& right)
                  { return left.entityKey < right.entityKey; });
    }

    std::span<const WorldMovementEntityUpdate> WorldMovementPhaseResult::Updates() const noexcept
    {
        return updates_;
    }
} // namespace psnr::world
