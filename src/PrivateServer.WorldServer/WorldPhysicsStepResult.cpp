#include "pch.h"

#include "WorldPhysicsStepResult.h"

#include <utility>

namespace psnr::world
{
    WorldPhysicsStepResult::WorldPhysicsStepResult(std::vector<WorldResolvedMotion> resolvedMotions,
                                                   std::vector<WorldPhysicsContact> contacts,
                                                   std::vector<WorldTriggerOverlap> triggerOverlaps) noexcept
        : resolvedMotions_(std::move(resolvedMotions))
        , contacts_(std::move(contacts))
        , triggerOverlaps_(std::move(triggerOverlaps))
    {
    }

    std::span<const WorldResolvedMotion> WorldPhysicsStepResult::ResolvedMotions() const noexcept
    {
        return resolvedMotions_;
    }

    std::span<const WorldPhysicsContact> WorldPhysicsStepResult::Contacts() const noexcept
    {
        return contacts_;
    }

    std::span<const WorldTriggerOverlap> WorldPhysicsStepResult::TriggerOverlaps() const noexcept
    {
        return triggerOverlaps_;
    }
} // namespace psnr::world
