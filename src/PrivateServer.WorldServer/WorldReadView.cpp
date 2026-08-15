#include "pch.h"

#include "WorldReadView.h"

namespace psnr::world
{
    bool WorldReadView::TryReadEntity(const WorldEntityKey entityKey, EntityHandle* const outHandle,
                                      WorldEntityComponents* const outComponents) const
    {
        if (outHandle == nullptr || outComponents == nullptr)
        {
            return false;
        }

        EntityHandle handle;
        if (!entityManager_.TryFindHandle(entityKey, &handle) ||
            !entityManager_.TryReadComponents(handle, outComponents))
        {
            return false;
        }

        *outHandle = handle;
        return true;
    }

    bool WorldReadView::TryReadComponents(const WorldEntityKey entityKey,
                                          WorldEntityComponents* const outComponents) const
    {
        if (outComponents == nullptr)
        {
            return false;
        }

        EntityHandle handle;
        if (!entityManager_.TryFindHandle(entityKey, &handle))
        {
            return false;
        }

        return entityManager_.TryReadComponents(handle, outComponents);
    }

    std::size_t WorldReadView::EntityCount() const noexcept
    {
        return entityManager_.Size();
    }
} // namespace psnr::world
