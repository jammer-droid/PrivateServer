#pragma once

#include "WorldEntityComponents.h"
#include "WorldEntityIdentity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    // EntityHandle.slotIndex로 sparse mapping을 찾고 실제 component는 dense하게 보관한다.
    // sparse mapping에 slotGeneration을 함께 보관해 제거되거나 재사용된 handle의 접근을 거부한다.
    class WorldEntityComponentStore final
    {
    public:
        [[nodiscard]] bool TryCreate(EntityHandle handle, const WorldEntityComponents& components);
        [[nodiscard]] bool TryRead(EntityHandle handle, WorldEntityComponents* outComponents) const;
        // Returned view is valid only until the next component-store mutation.
        [[nodiscard]] bool TryReadView(EntityHandle handle, const WorldEntityComponents** outComponents) const noexcept;
        [[nodiscard]] bool TryReplace(EntityHandle handle, const WorldEntityComponents& components);

        bool Remove(EntityHandle handle);

        [[nodiscard]] std::span<const EntityHandle> ActiveHandles() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;

    private:
        struct SparseSlot final
        {
            std::uint32_t denseIndex = 0;
            std::uint32_t slotGeneration = 0; // 0 == unused

            [[nodiscard]] bool IsUsed() const noexcept
            {
                return slotGeneration != 0;
            }
        };

        [[nodiscard]] bool IsAlive(EntityHandle handle) const noexcept;

        // 조회 경로
        //
        // EntityHandle { slotIndex: 3, slotGeneration: 1 }
        //                         |
        //                         v
        // sparseSlots_[3] { denseIndex: 1, slotGeneration: 1 }
        //                         |
        //              +----------+----------+
        //              |                     |
        //              v                     v
        // denseHandles_[1]          denseComponents_[1]
        // { slotIndex: 3, ... }      Entity slot 3의 component 묶음
        //
        // 배열 배치 예시: Entity slot 1이 제거된 상태
        //  - slotIndex 1 은 free_list 로 관리되고, EntityHandle에서 재사용되면 sparse는 같은 index를 재사용
        //  - 재사용시 denseIndex 는 가장 마지막 Index가 할당
        //
        // sparseSlots_
        //   slotIndex 0  -> denseIndex 0
        //   slotIndex 1  -> unused
        //   slotIndex 2  -> denseIndex 2
        //   slotIndex 3  -> denseIndex 1
        //
        // dense index          0               1               2
        // denseHandles_     [slotIndex 0]   [slotIndex 3]   [slotIndex 2]
        // denseComponents_  [entity 0]      [entity 3]      [entity 2]
        //
        // sparseSlots_ index는 EntityHandle.slotIndex와 1:1로 대응한다.
        // denseHandles_와 denseComponents_는 같은 dense index의 entity 정보다.
        // 중간 entity를 제거하면 마지막 dense 원소를 빈 위치로 옮기고,
        // 이동한 handle의 SparseSlot.denseIndex를 새 위치로 갱신한다.

        std::vector<SparseSlot> sparseSlots_;
        std::vector<EntityHandle> denseHandles_;
        std::vector<WorldEntityComponents> denseComponents_;
    };
} // namespace psnr::world
