#include "pch.h"

#include "WorldMovementPhaseCommitter.h"

#include <cassert>
#include <span>

namespace psnr::world
{
    void WorldMovementPhaseCommitter::Commit(const WorldMovementPhaseResult& result, WorldEntityManager& entityManager)
    {
        const std::span<const WorldMovementEntityUpdate> updates = result.Updates();
        for (const WorldMovementEntityUpdate& update : updates)
        {
            EntityHandle handle;
            WorldEntityComponents components;
            const bool handleFound = entityManager.TryFindHandle(update.entityKey, &handle);
            assert(handleFound);
            if (!handleFound)
            {
                return;
            }

            const bool componentsRead = entityManager.TryReadComponents(handle, &components);
            assert(componentsRead);
            if (!componentsRead)
            {
                return;
            }

            components.transform = update.transform;
            components.motion = update.motion;

            const bool replaced = entityManager.TryReplaceComponents(handle, components);
            assert(replaced);
            if (!replaced)
            {
                return;
            }
        }
    }
} // namespace psnr::world
