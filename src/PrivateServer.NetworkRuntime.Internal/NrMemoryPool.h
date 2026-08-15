#pragma once

#include "NrConcurrency.h"
#include "NrResult.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace psnr::core
{
    class NrMemoryPool;

    struct NrMemoryPoolConfig
    {
        std::size_t blockSize = 0;  // block 하나의 크기(byte)
        std::size_t blockCount = 0; // pool이 관리할 block 개수
        std::size_t alignment = 0;  // pool에서 빌려주는 각 block의 시작 주소 바이트 경계값 설정
    };

    struct NrMemoryPoolStats
    {
        std::size_t capacity = 0;           // 전체 block 수
        std::size_t inUse = 0;              // 현재 빌려간 block 수
        std::size_t available = 0;          // 빌려줄 수 있는 남은 block 수
        std::size_t highWatermark = 0;      // 동시에 가장 많이 사용된 block 수의 최고 기록
        std::uint64_t failedAcquireCount = 0; // block 부족으로 인해 acquire 실패한 횟수, saturating
    };

    class NrPooledMemoryBlock
    {
    public:
        NrPooledMemoryBlock() noexcept = default;

        NrPooledMemoryBlock(const NrPooledMemoryBlock&) = delete;
        NrPooledMemoryBlock& operator=(const NrPooledMemoryBlock&) = delete;

        NrPooledMemoryBlock(NrPooledMemoryBlock&& other) noexcept;
        NrPooledMemoryBlock& operator=(NrPooledMemoryBlock&& other) noexcept;

        ~NrPooledMemoryBlock() noexcept;

        [[nodiscard]] std::byte* Data() noexcept
        {
            return block_;
        }

        [[nodiscard]] const std::byte* Data() const noexcept
        {
            return block_;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return block_ != nullptr;
        }

        [[nodiscard]] std::size_t Capacity() const noexcept;
        [[nodiscard]] std::size_t Stride() const noexcept;

        [[nodiscard]] NrStatus Reset() noexcept;

    private:
        friend class NrMemoryPool;

        NrPooledMemoryBlock(NrMemoryPool* pool, std::byte* block) noexcept;

    private:
        NrMemoryPool* pool_ = nullptr;
        std::byte* block_ = nullptr;
    };

    class NrMemoryPool
    {
        friend class NrMemoryPoolTestAccess;

    public:
        NrMemoryPool(const NrMemoryPool&) = delete;
        NrMemoryPool& operator=(const NrMemoryPool&) = delete;

        NrMemoryPool(NrMemoryPool&&) = delete;
        NrMemoryPool& operator=(NrMemoryPool&&) = delete;

        ~NrMemoryPool() noexcept;

        [[nodiscard]] static NrResult<std::unique_ptr<NrMemoryPool>> Create(const NrMemoryPoolConfig& config);

        [[nodiscard]] NrResult<std::byte*> Acquire() noexcept;
        [[nodiscard]] NrResult<NrPooledMemoryBlock> AcquireBlock() noexcept;
        [[nodiscard]] NrStatus Release(std::byte* block) noexcept;

        [[nodiscard]] const NrMemoryPoolConfig& Config() const noexcept
        {
            return config_;
        }

        [[nodiscard]] NrMemoryPoolStats Stats() const noexcept;

        [[nodiscard]] std::size_t BlockStride() const noexcept
        {
            return blockStride_;
        }

        [[nodiscard]] std::size_t StorageSize() const noexcept
        {
            return storageSize_;
        }

    private:
        // storage_ 해제 정책용 function object
        // _aligned_malloc으로 잡은 메모리는 _aligned_free로 해제
        struct NrAlignedFreeDeleter
        {
            void operator()(std::byte* storage) const noexcept;
        };

        // block 메타 데이터
        struct NrMemoryBlockMetadata
        {
            bool inUse = false;
        };

        // use custom deleter
        using NrStoragePtr = std::unique_ptr<std::byte, NrAlignedFreeDeleter>;

        explicit NrMemoryPool(const NrMemoryPoolConfig& config) noexcept;

        [[nodiscard]] NrStatus InitializeStorage() noexcept; // 실제 메모리 할당

        [[nodiscard]] static bool IsValidConfig(const NrMemoryPoolConfig& config) noexcept;

        [[nodiscard]] bool TryResolveBlockIndex(std::byte* block, std::size_t& index) const noexcept;

    private:
        NrMemoryPoolConfig config_;
        NrMemoryPoolStats stats_; // for pool snapshot

        std::size_t blockStride_ = 0; // 풀 내부 block 정렬 기준
        std::size_t storageSize_ = 0; // 풀 storage 사이즈

        NrStoragePtr storage_;                        // 실제 메모리 덩어리
        std::vector<NrMemoryBlockMetadata> metadata_; // block 상태 목록
        std::vector<std::size_t> freeBlockIndices_;   // 빌려줄 수 있는 block 번호 목록(stack)
        // block addr = storageBase + (index * stride)

        mutable NrMutex mutex_;
    };

} // namespace psnr::core
