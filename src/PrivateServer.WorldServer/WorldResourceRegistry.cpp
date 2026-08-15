#include "pch.h"

#include "WorldResourceRegistry.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

namespace psnr::world
{
    bool WorldResourceInstance::ContainsAmbientSlot(const std::span<const WorldResourceInstance> resources,
                                                    const std::uint32_t ambientSlotId) noexcept
    {
        if (ambientSlotId == 0)
        {
            return false;
        }
        for (const WorldResourceInstance& resource : resources)
        {
            if (resource.origin == WorldResourceOrigin::Ambient && resource.ambientSlotId == ambientSlotId)
            {
                return true;
            }
        }
        return false;
    }

    namespace
    {
        [[nodiscard]] bool PositionLess(const WorldResourcePosition& left, const WorldResourcePosition& right) noexcept
        {
            if (left.positionX != right.positionX)
            {
                return left.positionX < right.positionX;
            }
            return left.positionY < right.positionY;
        }

        [[nodiscard]] bool IsValid(const WorldResourceInstance& instance) noexcept
        {
            if (!instance.entityKey.IsValid() || !instance.entityHandle.IsValid() ||
                !std::isfinite(instance.positionX) || !std::isfinite(instance.positionY))
            {
                return false;
            }
            if (instance.origin == WorldResourceOrigin::Ambient)
            {
                return instance.ambientSlotId != 0;
            }
            if (instance.origin == WorldResourceOrigin::DeathDrop)
            {
                return instance.ambientSlotId == 0;
            }
            return false;
        }
    } // namespace

    WorldResourceReserveResult WorldResourceRegistry::ReserveAdditional(const std::size_t additionalCount) noexcept
    {
        if (additionalCount > resources_.max_size() - resources_.size() ||
            additionalCount > positions_.max_size() - positions_.size())
        {
            return WorldResourceReserveResult::CapacityExceeded;
        }

        try
        {
            resources_.reserve(resources_.size() + additionalCount);
            positions_.reserve(positions_.size() + additionalCount);
            return WorldResourceReserveResult::Reserved;
        }
        catch (const std::length_error&)
        {
            return WorldResourceReserveResult::CapacityExceeded;
        }
        catch (const std::bad_alloc&)
        {
            return WorldResourceReserveResult::AllocationFailed;
        }
    }

    WorldResourceRegisterResult WorldResourceRegistry::TryRegister(const WorldResourceInstance& instance) noexcept
    {
        if (!IsValid(instance))
        {
            return WorldResourceRegisterResult::InvalidArgument;
        }

        const std::vector<WorldResourceInstance>::iterator resourceInsertionPoint =
            std::lower_bound(resources_.begin(), resources_.end(), instance.entityKey,
                             [](const WorldResourceInstance& resource, const WorldEntityKey expectedKey) noexcept
                             { return resource.entityKey < expectedKey; });
        if (resourceInsertionPoint != resources_.end() && resourceInsertionPoint->entityKey == instance.entityKey)
        {
            return WorldResourceRegisterResult::DuplicateEntity;
        }
        for (const WorldResourceInstance& resource : resources_)
        {
            if (resource.entityHandle == instance.entityHandle)
            {
                return WorldResourceRegisterResult::DuplicateEntity;
            }
            if (instance.origin == WorldResourceOrigin::Ambient && resource.origin == WorldResourceOrigin::Ambient &&
                resource.ambientSlotId == instance.ambientSlotId)
            {
                return WorldResourceRegisterResult::DuplicateEntity;
            }
        }

        const WorldResourcePosition position{instance.positionX, instance.positionY};
        const std::vector<WorldResourcePosition>::iterator positionInsertionPoint =
            std::lower_bound(positions_.begin(), positions_.end(), position, PositionLess);
        if (positionInsertionPoint != positions_.end() && *positionInsertionPoint == position)
        {
            return WorldResourceRegisterResult::DuplicatePosition;
        }

        bool resourceInserted = false;
        try
        {
            static_cast<void>(resources_.insert(resourceInsertionPoint, instance));
            resourceInserted = true;
            static_cast<void>(positions_.insert(positionInsertionPoint, position));
            return WorldResourceRegisterResult::Registered;
        }
        catch (...)
        {
            if (resourceInserted)
            {
                const std::vector<WorldResourceInstance>::iterator inserted = std::lower_bound(
                    resources_.begin(), resources_.end(), instance.entityKey,
                    [](const WorldResourceInstance& resource, const WorldEntityKey expectedKey) noexcept
                    { return resource.entityKey < expectedKey; });
                if (inserted != resources_.end() && inserted->entityKey == instance.entityKey)
                {
                    static_cast<void>(resources_.erase(inserted));
                }
            }
            return WorldResourceRegisterResult::AllocationFailed;
        }
    }

    WorldResourceRemoveResult WorldResourceRegistry::TryRemove(const WorldEntityKey entityKey,
                                                               const EntityHandle entityHandle) noexcept
    {
        if (!entityKey.IsValid() || !entityHandle.IsValid())
        {
            return WorldResourceRemoveResult::InvalidArgument;
        }

        const std::vector<WorldResourceInstance>::iterator found =
            std::lower_bound(resources_.begin(), resources_.end(), entityKey,
                             [](const WorldResourceInstance& resource, const WorldEntityKey expectedKey) noexcept
                             { return resource.entityKey < expectedKey; });
        if (found == resources_.end() || found->entityKey != entityKey)
        {
            return WorldResourceRemoveResult::NotFound;
        }
        if (found->entityHandle != entityHandle)
        {
            return WorldResourceRemoveResult::IdentityMismatch;
        }

        const WorldResourcePosition position{found->positionX, found->positionY};
        const std::vector<WorldResourcePosition>::iterator positionFound =
            std::lower_bound(positions_.begin(), positions_.end(), position, PositionLess);
        if (positionFound == positions_.end() || *positionFound != position)
        {
            return WorldResourceRemoveResult::StateInvariantViolation;
        }

        static_cast<void>(positions_.erase(positionFound));
        static_cast<void>(resources_.erase(found));
        return WorldResourceRemoveResult::Removed;
    }

    bool WorldResourceRegistry::TryFind(const WorldEntityKey entityKey,
                                        WorldResourceInstance* const outInstance) const noexcept
    {
        if (!entityKey.IsValid() || outInstance == nullptr)
        {
            return false;
        }

        const std::vector<WorldResourceInstance>::const_iterator found =
            std::lower_bound(resources_.begin(), resources_.end(), entityKey,
                             [](const WorldResourceInstance& resource, const WorldEntityKey expectedKey) noexcept
                             { return resource.entityKey < expectedKey; });
        if (found == resources_.end() || found->entityKey != entityKey)
        {
            return false;
        }

        *outInstance = *found;
        return true;
    }

    bool WorldResourceRegistry::TryFindAmbientSlot(const std::uint32_t ambientSlotId,
                                                   WorldResourceInstance* const outInstance) const noexcept
    {
        if (ambientSlotId == 0 || outInstance == nullptr)
        {
            return false;
        }

        for (const WorldResourceInstance& resource : resources_)
        {
            if (resource.origin == WorldResourceOrigin::Ambient && resource.ambientSlotId == ambientSlotId)
            {
                *outInstance = resource;
                return true;
            }
        }
        return false;
    }

    bool WorldResourceRegistry::ContainsExactPosition(const float positionX, const float positionY) const noexcept
    {
        if (!std::isfinite(positionX) || !std::isfinite(positionY))
        {
            return false;
        }

        const WorldResourcePosition position{positionX, positionY};
        const std::vector<WorldResourcePosition>::const_iterator found =
            std::lower_bound(positions_.begin(), positions_.end(), position, PositionLess);
        return found != positions_.end() && *found == position;
    }

    std::span<const WorldResourceInstance> WorldResourceRegistry::Instances() const noexcept
    {
        return resources_;
    }

    std::size_t WorldResourceRegistry::Count() const noexcept
    {
        return resources_.size();
    }
} // namespace psnr::world
