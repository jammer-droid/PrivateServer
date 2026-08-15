#pragma once

#include "WorldEntityIdentity.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace psnr::world
{
    // 하나의 entity slot에서 외부 WorldEntityKey와 내부 EntityHandle을 함께 발급하고 수명을 관리한다.
    // gameplay component는 이 registry에서 받은 EntityHandle을 내부 참조로 사용한다.
    class WorldEntityRegistry final
    {
    public:
        [[nodiscard]] bool TryCreate(WorldEntityKey* outKey, EntityHandle* outHandle);
        [[nodiscard]] bool TryFindHandle(WorldEntityKey key, EntityHandle* outHandle) const;
        [[nodiscard]] bool TryFindKey(EntityHandle handle, WorldEntityKey* outKey) const;

        bool Remove(EntityHandle handle);

        [[nodiscard]] std::size_t Size() const noexcept;

    private:
        struct EntitySlot final
        {
            std::uint32_t entityGeneration = 0; // for Entity Key
            std::uint32_t slotGeneration = 0;   // for Entity Slot
            bool isUsed = false;

            [[nodiscard]] bool IsUsed() const noexcept
            {
                return isUsed;
            }
        };

        [[nodiscard]] bool IsAlive(EntityHandle handle) const noexcept;

        // WorldEntityKey.entityId는 slot index + 1이며 EntityHandle.slotIndex는 slot index다.
        std::vector<EntitySlot> slots_;
        std::vector<std::uint32_t> freeSlotIndices_;
        std::size_t activeCount_ = 0;
    };
} // namespace psnr::world
