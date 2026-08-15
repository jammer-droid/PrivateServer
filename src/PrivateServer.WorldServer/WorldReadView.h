#pragma once

#include "WorldEntityManager.h"

#include <cstddef>

namespace psnr::world
{
    // Tick compute phase가 canonical entity state를 직접 변경하지 않고 읽기만 하는 facade다.
    // View가 사용되는 동안 World owner는 entity manager를 변경하지 않는다.
    class WorldReadView final
    {
    public:
        explicit WorldReadView(const WorldEntityManager& entityManager) noexcept
            : entityManager_(entityManager)
        {
        }

        [[nodiscard]] bool TryReadEntity(WorldEntityKey entityKey, EntityHandle* outHandle,
                                         WorldEntityComponents* outComponents) const;
        [[nodiscard]] bool TryReadComponents(WorldEntityKey entityKey, WorldEntityComponents* outComponents) const;
        [[nodiscard]] std::size_t EntityCount() const noexcept;

    private:
        const WorldEntityManager& entityManager_;
    };
} // namespace psnr::world
