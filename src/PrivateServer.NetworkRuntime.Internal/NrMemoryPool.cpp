#include "pch.h"

#include "NrMemoryPool.h"

#include "NrMemoryMath.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <malloc.h>
#include <new>
#include <utility>

namespace psnr::core
{
    NrPooledMemoryBlock::NrPooledMemoryBlock(NrPooledMemoryBlock&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr))
        , block_(std::exchange(other.block_, nullptr))
    {
    }

    NrPooledMemoryBlock& NrPooledMemoryBlock::operator=(NrPooledMemoryBlock&& other) noexcept
    {
        if (this != &other)
        {
            (void)Reset();
            pool_ = std::exchange(other.pool_, nullptr);
            block_ = std::exchange(other.block_, nullptr);
        }

        return *this;
    }

    NrPooledMemoryBlock::~NrPooledMemoryBlock() noexcept
    {
        (void)Reset();
    }

    std::size_t NrPooledMemoryBlock::Capacity() const noexcept
    {
        return pool_ != nullptr ? pool_->Config().blockSize : 0;
    }

    std::size_t NrPooledMemoryBlock::Stride() const noexcept
    {
        return pool_ != nullptr ? pool_->BlockStride() : 0;
    }

    NrStatus NrPooledMemoryBlock::Reset() noexcept
    {
        if (pool_ == nullptr || block_ == nullptr)
        {
            return NrStatus();
        }

        NrMemoryPool* pool = std::exchange(pool_, nullptr);
        std::byte* block = std::exchange(block_, nullptr);
        return pool->Release(block);
    }

    NrPooledMemoryBlock::NrPooledMemoryBlock(NrMemoryPool* pool, std::byte* block) noexcept
        : pool_(pool)
        , block_(block)
    {
    }

    NrMemoryPool::~NrMemoryPool() noexcept = default;

    NrResult<std::unique_ptr<NrMemoryPool>> NrMemoryPool::Create(const NrMemoryPoolConfig& config)
    {
        if (!IsValidConfig(config))
        {
            return NrResult<std::unique_ptr<NrMemoryPool>>::Failure(NrErrorCode::InvalidArgument);
        }

        std::unique_ptr<NrMemoryPool> pool(new (std::nothrow)
                                               NrMemoryPool(config)); // private constructor 라 new 직접 사용
        if (pool == nullptr)
        {
            // nothrow allocation failure path.
            return NrResult<std::unique_ptr<NrMemoryPool>>::Failure(NrErrorCode::OutOfMemory);
        }

        const NrStatus initStatus = pool->InitializeStorage();
        if (initStatus.Failed())
        {
            return NrResult<std::unique_ptr<NrMemoryPool>>::Failure(initStatus);
        }

        return NrResult<std::unique_ptr<NrMemoryPool>>(std::move(pool));
    }

    NrResult<std::byte*> NrMemoryPool::Acquire() noexcept
    {
        NrScopedLock<NrMutex> lock(mutex_);

        if (freeBlockIndices_.empty())
        {
            constexpr std::uint64_t MaxFailedAcquireCount = std::numeric_limits<std::uint64_t>::max();
            if (stats_.failedAcquireCount != MaxFailedAcquireCount)
            {
                ++stats_.failedAcquireCount;
            }
            return NrResult<std::byte*>::Failure(NrErrorCode::PoolExhausted);
        }

        const std::size_t blockIndex = freeBlockIndices_.back();
        freeBlockIndices_.pop_back();

        metadata_[blockIndex].inUse = true;
        ++stats_.inUse;
        --stats_.available;
        stats_.highWatermark = std::max(stats_.highWatermark, stats_.inUse);

        return NrResult<std::byte*>(storage_.get() + (blockIndex * blockStride_));
    }

    NrResult<NrPooledMemoryBlock> NrMemoryPool::AcquireBlock() noexcept
    {
        NrResult<std::byte*> acquireResult = Acquire();
        if (acquireResult.Failed())
        {
            return NrResult<NrPooledMemoryBlock>::Failure(acquireResult.Status());
        }

        return NrResult<NrPooledMemoryBlock>(NrPooledMemoryBlock(this, acquireResult.TakeValue()));
    }

    NrStatus NrMemoryPool::Release(std::byte* block) noexcept
    {
        NrScopedLock<NrMutex> lock(mutex_);

        std::size_t blockIndex = 0;
        if (!TryResolveBlockIndex(block, blockIndex))
        {
            return NrStatus(NrErrorCode::InvalidArgument);
        }

        NrMemoryBlockMetadata& metadata = metadata_[blockIndex];
        if (!metadata.inUse)
        {
            return NrStatus(NrErrorCode::InvalidState);
        }

        metadata.inUse = false;
        freeBlockIndices_.push_back(blockIndex);
        --stats_.inUse;
        ++stats_.available;

        return NrStatus();
    }

    NrMemoryPoolStats NrMemoryPool::Stats() const noexcept
    {
        NrScopedLock<NrMutex> lock(mutex_);
        return stats_;
    }

    void NrMemoryPool::NrAlignedFreeDeleter::operator()(std::byte* storage) const noexcept
    {
        _aligned_free(storage);
    }

    NrMemoryPool::NrMemoryPool(const NrMemoryPoolConfig& config) noexcept
        : config_(config)
        , stats_{
              config.blockCount, 0, config.blockCount, 0, 0,
          }
    {
    }

    NrStatus NrMemoryPool::InitializeStorage() noexcept
    {
        // 메모리 풀 내부 block align stride 계산
        std::size_t blockStride = 0;
        if (!utils::NrTryAlignUp(config_.blockSize, config_.alignment, blockStride))
        {
            return NrStatus(NrErrorCode::CapacityExceeded);
        }

        std::size_t storageSize = 0;
        if (!utils::NrTryMultiply(blockStride, config_.blockCount, storageSize))
        {
            return NrStatus(NrErrorCode::CapacityExceeded);
        }

        // 전체 storage 시작 주소 정렬
        // _aligned_malloc에서는 alignment(주소 정렬)를 다음과 같이 결정
        // - 최소 sizeof(void*) 이상. (64비트 프로세스 정렬 단위를 최소로 설정)
        // - config_.alignment는 2의 거듭제곱. (_aligned_malloc 요구 조건)
        const std::size_t allocationAlignment = std::max(config_.alignment, sizeof(void*));
        std::byte* storage = static_cast<std::byte*>(_aligned_malloc(storageSize, allocationAlignment)); // MSVC only
        if (storage == nullptr)
        {
            return NrStatus(NrErrorCode::OutOfMemory);
        }

        storage_.reset(storage);
        blockStride_ = blockStride;
        storageSize_ = storageSize;

        try
        {
            metadata_.resize(config_.blockCount);
            freeBlockIndices_.reserve(config_.blockCount);
        }
        catch (const std::bad_alloc&) // noexcept 처리
        {
            return NrStatus(NrErrorCode::OutOfMemory);
        }

        for (std::size_t index = config_.blockCount; index > 0; --index)
        {
            freeBlockIndices_.push_back(index - 1);
        }

        return NrStatus();
    }

    bool NrMemoryPool::IsValidConfig(const NrMemoryPoolConfig& config) noexcept
    {
        return config.blockSize > 0 && config.blockCount > 0 && utils::NrIsPowerOfTwo(config.alignment);
    }

    bool NrMemoryPool::TryResolveBlockIndex(std::byte* block, std::size_t& index) const noexcept
    {
        if (block == nullptr || storage_ == nullptr || blockStride_ == 0)
        {
            return false;
        }

        // 0. block 주소 = base + (index * stride)
        //      -> block - base = (index * stride)
        //      -> index = (block - base) / stride;

        // 1. storage_ 메모리의 시작 주소 가져오기
        const std::uintptr_t storageStart = reinterpret_cast<std::uintptr_t>(storage_.get());
        // 2. block 의 시작 주소 가져오기
        const std::uintptr_t blockAddress = reinterpret_cast<std::uintptr_t>(block);
        if (blockAddress < storageStart)
        {
            return false;
        }

        // 3. offset(block - base) 계산
        const std::uintptr_t offset = blockAddress - storageStart;
        if (offset >= storageSize_ || (offset % blockStride_) != 0)
        {
            return false;
        }

        // 4. stride로 나눠 index 계산
        index = static_cast<std::size_t>(offset / blockStride_);
        return index < metadata_.size();
    }

} // namespace psnr::core
