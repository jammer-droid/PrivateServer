#include "pch.h"

#include "NrMemoryPoolManager.h"

#include <cstddef>
#include <new>
#include <utility>

namespace psnr::core
{
    NrMemoryPoolManager::~NrMemoryPoolManager() noexcept = default;

    NrResult<std::unique_ptr<NrMemoryPoolManager>> NrMemoryPoolManager::Create(const NrMemoryPoolManagerConfig& config)
    {
        NrPoolArray pools;
        std::array<bool, NrMemoryPoolRoleCount> initialized{};
        bool hasPool = false;

        for (const NrMemoryPoolManagerPoolConfig& poolConfig : config.pools)
        {
            if (poolConfig.role == NrMemoryPoolRole::Count)
            {
                continue;
            }

            std::size_t poolIndex = 0;
            if (!TryGetPoolIndex(poolConfig.role, poolIndex) || initialized[poolIndex])
            {
                // invalid index
                return NrResult<std::unique_ptr<NrMemoryPoolManager>>::Failure(NrErrorCode::InvalidArgument);
            }

            NrResult<std::unique_ptr<NrMemoryPool>> poolResult = NrMemoryPool::Create(poolConfig.pool);
            if (poolResult.Failed())
            {
                return NrResult<std::unique_ptr<NrMemoryPoolManager>>::Failure(poolResult.Status());
            }

            pools[poolIndex] = poolResult.TakeValue(); // std::unique_ptr<NrMemoryPool>
            initialized[poolIndex] = true;
            hasPool = true;
        }

        if (!hasPool)
        {
            return NrResult<std::unique_ptr<NrMemoryPoolManager>>::Failure(NrErrorCode::InvalidArgument);
        }

        std::unique_ptr<NrMemoryPoolManager> manager(new (std::nothrow) NrMemoryPoolManager(std::move(pools)));
        if (manager == nullptr)
        {
            // std::bad_alloc case
            return NrResult<std::unique_ptr<NrMemoryPoolManager>>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<std::unique_ptr<NrMemoryPoolManager>>(std::move(manager));
    }

    NrResult<NrPooledMemoryBlock> NrMemoryPoolManager::AcquireBlock(NrMemoryPoolRole role) noexcept
    {
        NrMemoryPool* pool = ResolvePool(role);
        if (pool == nullptr)
        {
            return NrResult<NrPooledMemoryBlock>::Failure(IsKnownRole(role) ? NrErrorCode::InvalidState
                                                                            : NrErrorCode::InvalidArgument);
        }

        return pool->AcquireBlock();
    }

    NrResult<NrMemoryPoolStats> NrMemoryPoolManager::Stats(NrMemoryPoolRole role) const noexcept
    {
        const NrMemoryPool* pool = ResolvePool(role);
        if (pool == nullptr)
        {
            return NrResult<NrMemoryPoolStats>::Failure(IsKnownRole(role) ? NrErrorCode::InvalidState
                                                                          : NrErrorCode::InvalidArgument);
        }

        return NrResult<NrMemoryPoolStats>(pool->Stats());
    }

    NrMemoryPoolManagerStats NrMemoryPoolManager::Stats() const noexcept
    {
        NrMemoryPoolManagerStats stats;

        for (std::size_t index = 0; index < NrMemoryPoolRoleCount; ++index)
        {
            stats.pools[index] = NrMemoryPoolManagerStatsEntry{
                static_cast<NrMemoryPoolRole>(index),
                pools_[index] == nullptr ? NrMemoryPoolStats{} : pools_[index]->Stats(),
            };
        }

        return stats;
    }

    NrMemoryPoolManager::NrMemoryPoolManager(NrPoolArray pools) noexcept
        : pools_(std::move(pools)) // call std::array's move constructor
    {
    }

    NrMemoryPool* NrMemoryPoolManager::ResolvePool(NrMemoryPoolRole role) noexcept
    {
        std::size_t poolIndex = 0;
        if (!TryGetPoolIndex(role, poolIndex))
        {
            return nullptr;
        }

        return pools_[poolIndex].get();
    }

    const NrMemoryPool* NrMemoryPoolManager::ResolvePool(NrMemoryPoolRole role) const noexcept
    {
        std::size_t poolIndex = 0;
        if (!TryGetPoolIndex(role, poolIndex))
        {
            return nullptr;
        }

        return pools_[poolIndex].get();
    }

    bool NrMemoryPoolManager::TryGetPoolIndex(NrMemoryPoolRole role, std::size_t& index) noexcept
    {
        const std::size_t roleIndex = static_cast<std::size_t>(role);
        if (roleIndex < NrMemoryPoolRoleCount)
        {
            index = roleIndex;
            return true;
        }

        return false;
    }

    bool NrMemoryPoolManager::IsKnownRole(NrMemoryPoolRole role) noexcept
    {
        std::size_t unusedIndex = 0;
        return TryGetPoolIndex(role, unusedIndex);
    }

} // namespace psnr::core
