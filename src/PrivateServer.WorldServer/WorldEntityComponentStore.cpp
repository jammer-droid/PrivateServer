#include "pch.h"

#include "WorldEntityComponentStore.h"

#include <limits>
#include <utility>

namespace psnr::world
{
    bool WorldEntityComponentStore::TryCreate(const EntityHandle handle, const WorldEntityComponents& components)
    {
        if (!handle.IsValid() || handle.slotIndex > sparseSlots_.size() ||
            denseComponents_.size() >= std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }

        if (handle.slotIndex == sparseSlots_.size()) // WorldEntityRegistry 에서 새로운 slot 을 사용
        {
            sparseSlots_.push_back(SparseSlot{}); // 대응하는 sparse slot 추가
        }

        SparseSlot& sparseSlot = sparseSlots_[handle.slotIndex];
        if (sparseSlot.IsUsed()) // create 중에 used 면 false
        {
            return false;
        }

        const std::uint32_t denseIndex = static_cast<std::uint32_t>(denseComponents_.size()); // dense에는 순차적으로
        denseHandles_.push_back(handle);
        denseComponents_.push_back(components);

        sparseSlot.denseIndex = denseIndex;
        sparseSlot.slotGeneration = handle.slotGeneration;
        return true;
    }

    bool WorldEntityComponentStore::TryRead(const EntityHandle handle, WorldEntityComponents* const outComponents) const
    {
        if (outComponents == nullptr || !IsAlive(handle))
        {
            return false;
        }

        const SparseSlot& sparseSlot = sparseSlots_[handle.slotIndex];
        *outComponents = denseComponents_[sparseSlot.denseIndex];
        return true;
    }

    bool WorldEntityComponentStore::TryReadView(const EntityHandle handle,
                                                const WorldEntityComponents** const outComponents) const noexcept
    {
        if (outComponents == nullptr || !IsAlive(handle))
        {
            return false;
        }

        const SparseSlot& sparseSlot = sparseSlots_[handle.slotIndex];
        *outComponents = &denseComponents_[sparseSlot.denseIndex];
        return true;
    }

    bool WorldEntityComponentStore::TryReplace(const EntityHandle handle, const WorldEntityComponents& components)
    {
        if (!IsAlive(handle))
        {
            return false;
        }

        WorldEntityComponents committed = components;
        const SparseSlot& sparseSlot = sparseSlots_[handle.slotIndex];
        denseComponents_[sparseSlot.denseIndex] = std::move(committed);
        return true;
    }

    bool WorldEntityComponentStore::Remove(const EntityHandle handle)
    {
        if (!IsAlive(handle))
        {
            return false;
        }

        SparseSlot& removedSparseSlot = sparseSlots_[handle.slotIndex];
        const std::uint32_t removedDenseIndex = removedSparseSlot.denseIndex;
        const std::uint32_t lastDenseIndex = static_cast<std::uint32_t>(denseComponents_.size() - 1);

        if (removedDenseIndex != lastDenseIndex)
        {
            denseComponents_[removedDenseIndex] = std::move(denseComponents_[lastDenseIndex]);
            denseHandles_[removedDenseIndex] = denseHandles_[lastDenseIndex];

            const EntityHandle movedHandle = denseHandles_[removedDenseIndex];
            sparseSlots_[movedHandle.slotIndex].denseIndex = removedDenseIndex;
        }

        denseComponents_.pop_back();
        denseHandles_.pop_back();
        removedSparseSlot = {};
        return true;
    }

    std::size_t WorldEntityComponentStore::Size() const noexcept
    {
        return denseComponents_.size();
    }

    std::span<const EntityHandle> WorldEntityComponentStore::ActiveHandles() const noexcept
    {
        return denseHandles_;
    }

    bool WorldEntityComponentStore::IsAlive(const EntityHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.slotIndex >= sparseSlots_.size())
        {
            return false;
        }

        const SparseSlot& sparseSlot = sparseSlots_[handle.slotIndex];
        if (!sparseSlot.IsUsed() || sparseSlot.slotGeneration != handle.slotGeneration ||
            sparseSlot.denseIndex >= denseHandles_.size())
        {
            return false;
        }

        return denseHandles_[sparseSlot.denseIndex] == handle;
    }
} // namespace psnr::world
