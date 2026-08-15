#include "pch.h"

#include "WorldEntityRegistry.h"

#include <cassert>
#include <functional>
#include <limits>

namespace psnr::world
{
    bool WorldEntityRegistry::TryCreate(WorldEntityKey* const outKey, EntityHandle* const outHandle)
    {
        if (outKey == nullptr || outHandle == nullptr)
        {
            return false;
        }

        std::uint32_t slotIndex = 0;

        if (!freeSlotIndices_.empty())
        {
            slotIndex = freeSlotIndices_.back();
            freeSlotIndices_.pop_back();

            EntitySlot& slot = slots_[slotIndex];

            assert(!slot.IsUsed());
            assert(slot.entityGeneration < std::numeric_limits<std::uint32_t>::max());
            assert(slot.slotGeneration < std::numeric_limits<std::uint32_t>::max());

            ++slot.entityGeneration;
            ++slot.slotGeneration;
            slot.isUsed = true;
        }
        else
        {
            if (slots_.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }

            slotIndex = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(EntitySlot{1, 1, true});
        }

        const EntitySlot& slot = slots_[slotIndex];
        *outKey = WorldEntityKey{slotIndex + 1, slot.entityGeneration};
        *outHandle = EntityHandle{slotIndex, slot.slotGeneration};
        ++activeCount_;
        return true;
    }

    bool WorldEntityRegistry::TryFindHandle(const WorldEntityKey key, EntityHandle* const outHandle) const
    {
        if (outHandle == nullptr || !key.IsValid())
        {
            return false;
        }

        const std::uint32_t slotIndex = key.entityId - 1;
        if (slotIndex >= slots_.size())
        {
            return false;
        }

        const EntitySlot& slot = slots_[slotIndex];
        if (!slot.IsUsed() || slot.entityGeneration != key.generation)
        {
            return false;
        }

        *outHandle = EntityHandle{slotIndex, slot.slotGeneration};
        return true;
    }

    bool WorldEntityRegistry::TryFindKey(const EntityHandle handle, WorldEntityKey* const outKey) const
    {
        if (outKey == nullptr || !IsAlive(handle))
        {
            return false;
        }

        const EntitySlot& slot = slots_[handle.slotIndex];
        *outKey = WorldEntityKey{handle.slotIndex + 1, slot.entityGeneration};
        return true;
    }

    bool WorldEntityRegistry::Remove(const EntityHandle handle)
    {
        if (!IsAlive(handle))
        {
            return false;
        }

        EntitySlot& slot = slots_[handle.slotIndex];
        slot.isUsed = false;
        --activeCount_;

        // 어느 generation도 wrap-around하지 않도록 최대치에 도달한 slot은 재사용하지 않는다.
        if (slot.entityGeneration != std::numeric_limits<std::uint32_t>::max() &&
            slot.slotGeneration != std::numeric_limits<std::uint32_t>::max())
        {
            freeSlotIndices_.push_back(handle.slotIndex);
        }

        return true;
    }

    std::size_t WorldEntityRegistry::Size() const noexcept
    {
        return activeCount_;
    }

    /*
    WorldEntityKey 전용 hash 함수
    - entityId, generation 을 받아 uint64_t 로 합침
    - hash 충돌 발생하면 WorldEntityKey::operator== 으로 key 비교 추가 진행

         상위 32비트                하위 32비트
        ┌────────────────────────┬────────────────────────┐
        │ entityId = 10          │ generation = 2         │
        └────────────────────────┴────────────────────────┘
    */
    std::size_t WorldEntityKeyHash::operator()(const WorldEntityKey key) const noexcept
    {
        const std::uint64_t packedKey =
            (static_cast<std::uint64_t>(key.entityId) << 32) | static_cast<std::uint64_t>(key.generation);

        // cpp 표준 해시 함수로 key 반환(packedKey to unordered_map 에서 사용가능한 key로)
        return std::hash<std::uint64_t>{}(packedKey);
    }

    // handle 이 valid, handle 이 가리키는 slot 의 내용도 valid
    bool WorldEntityRegistry::IsAlive(const EntityHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.slotIndex >= slots_.size())
        {
            return false;
        }

        const EntitySlot& slot = slots_[handle.slotIndex];
        return slot.IsUsed() && slot.slotGeneration == handle.slotGeneration;
    }
} // namespace psnr::world
