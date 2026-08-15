#pragma once

#include "WorldEntityComponentStore.h"
#include "WorldEntityRegistry.h"

#include <cstddef>
#include <span>

namespace psnr::world
{
    // World entity의 logical identity, storage identity와 component lifetime을 함께 관리한다.
    // Session, tick, gameplay, physics와 replication lifecycle은 소유하지 않는다.
    class WorldEntityManager final
    {
    public:
        [[nodiscard]] bool TryCreate(const WorldEntityComponents& components, WorldEntityKey* outKey,
                                     EntityHandle* outHandle);

        [[nodiscard]] bool TryFindHandle(WorldEntityKey key, EntityHandle* outHandle) const;
        [[nodiscard]] bool TryFindKey(EntityHandle handle, WorldEntityKey* outKey) const;
        [[nodiscard]] bool TryReadComponents(EntityHandle handle, WorldEntityComponents* outComponents) const;
        // Returned view is valid only until the next entity/component mutation.
        [[nodiscard]] bool TryReadComponentsView(EntityHandle handle,
                                                 const WorldEntityComponents** outComponents) const noexcept;
        [[nodiscard]] bool TryReplaceComponents(EntityHandle handle, const WorldEntityComponents& components);

        bool Remove(EntityHandle handle);

        // World owner가 mutation하지 않는 compute 구간에서만 유효한 dense handle view다.
        [[nodiscard]] std::span<const EntityHandle> ActiveHandles() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;

    private:
        // Entity 생성 절차
        // 1. EntityRegistry에서 WorldEntityKey와 EntityHandle 발급
        // 2. ComponentStore 생성
        // 3. 모두 성공하면 호출자에게 key, handle 공개
        // 4. ComponentStore 생성 실패 시 EntityRegistry 등록 rollback

        WorldEntityRegistry entityRegistry_;
        WorldEntityComponentStore componentStore_;
    };
} // namespace psnr::world
