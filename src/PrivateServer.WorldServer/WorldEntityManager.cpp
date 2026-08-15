#include "pch.h"

#include "WorldEntityManager.h"

#include <cassert>

namespace psnr::world
{
    bool WorldEntityManager::TryCreate(const WorldEntityComponents& components, WorldEntityKey* const outKey,
                                       EntityHandle* const outHandle)
    {
        if (outKey == nullptr || outHandle == nullptr)
        {
            return false;
        }

        WorldEntityKey key;
        EntityHandle handle;
        if (!entityRegistry_.TryCreate(&key, &handle))
        {
            return false;
        }

        if (!componentStore_.TryCreate(handle, components))
        {
            const bool registryRollbackSucceeded = entityRegistry_.Remove(handle);
            assert(registryRollbackSucceeded);
            (void)registryRollbackSucceeded;
            return false;
        }

        *outKey = key;
        *outHandle = handle;
        return true;
    }

    bool WorldEntityManager::TryFindHandle(const WorldEntityKey key, EntityHandle* const outHandle) const
    {
        return entityRegistry_.TryFindHandle(key, outHandle);
    }

    bool WorldEntityManager::TryFindKey(const EntityHandle handle, WorldEntityKey* const outKey) const
    {
        return entityRegistry_.TryFindKey(handle, outKey);
    }

    bool WorldEntityManager::TryReadComponents(const EntityHandle handle,
                                               WorldEntityComponents* const outComponents) const
    {
        return componentStore_.TryRead(handle, outComponents);
    }

    bool WorldEntityManager::TryReadComponentsView(const EntityHandle handle,
                                                   const WorldEntityComponents** const outComponents) const noexcept
    {
        return componentStore_.TryReadView(handle, outComponents);
    }

    bool WorldEntityManager::TryReplaceComponents(const EntityHandle handle, const WorldEntityComponents& components)
    {
        return componentStore_.TryReplace(handle, components);
    }

    bool WorldEntityManager::Remove(const EntityHandle handle)
    {
        WorldEntityKey key;
        WorldEntityComponents components;
        if (!entityRegistry_.TryFindKey(handle, &key) || !componentStore_.TryRead(handle, &components))
        {
            return false;
        }

        if (!componentStore_.Remove(handle))
        {
            return false;
        }

        if (!entityRegistry_.Remove(handle))
        {
            const bool componentRollbackSucceeded = componentStore_.TryCreate(handle, components);
            assert(componentRollbackSucceeded);
            (void)componentRollbackSucceeded;
            return false;
        }

        return true;
    }

    std::size_t WorldEntityManager::Size() const noexcept
    {
        return entityRegistry_.Size();
    }

    std::span<const EntityHandle> WorldEntityManager::ActiveHandles() const noexcept
    {
        return componentStore_.ActiveHandles();
    }

} // namespace psnr::world
