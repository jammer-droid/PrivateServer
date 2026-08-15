#include "pch.h"

#include "NrMemoryPool.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace psnr::core
{
    class NrMemoryPoolTestAccess final
    {
    public:
        static void SetFailedAcquireCount(NrMemoryPool& pool, const std::uint64_t value) noexcept
        {
            NrScopedLock<NrMutex> lock(pool.mutex_);
            pool.stats_.failedAcquireCount = value;
        }
    };

    namespace
    {
        TEST(NrMemoryPoolTests, MemoryPoolIsNotCopyableOrMovable)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrMemoryPool>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrMemoryPool>);
            EXPECT_FALSE(std::is_move_constructible_v<NrMemoryPool>);
            EXPECT_FALSE(std::is_move_assignable_v<NrMemoryPool>);
        }

        TEST(NrMemoryPoolTests, PooledMemoryBlockIsMoveOnly)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrPooledMemoryBlock>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrPooledMemoryBlock>);
            EXPECT_TRUE(std::is_move_constructible_v<NrPooledMemoryBlock>);
            EXPECT_TRUE(std::is_move_assignable_v<NrPooledMemoryBlock>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrPooledMemoryBlock>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrPooledMemoryBlock>);
        }

        TEST(NrMemoryPoolTests, CreateWithValidConfigInitializesStats)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 1024;
            config.blockCount = 32;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            ASSERT_TRUE(result.Succeeded());
            ASSERT_TRUE(result.HasValue());

            std::unique_ptr<NrMemoryPool> pool = result.TakeValue();
            ASSERT_NE(pool, nullptr);

            const NrMemoryPoolStats stats = pool->Stats();

            EXPECT_EQ(stats.capacity, config.blockCount);
            EXPECT_EQ(stats.inUse, 0u);
            EXPECT_EQ(stats.available, config.blockCount);
            EXPECT_EQ(stats.highWatermark, 0u);
            EXPECT_EQ(stats.failedAcquireCount, 0u);
            EXPECT_EQ(pool->BlockStride(), config.blockSize);
            EXPECT_EQ(pool->StorageSize(), config.blockSize * config.blockCount);
        }

        TEST(NrMemoryPoolTests, CreateRoundsBlockStrideUpToAlignment)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 10;
            config.blockCount = 3;
            config.alignment = 8;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            ASSERT_TRUE(result.Succeeded());

            std::unique_ptr<NrMemoryPool> pool = result.TakeValue();

            EXPECT_EQ(pool->BlockStride(), 16u);
            EXPECT_EQ(pool->StorageSize(), 48u);
        }

        TEST(NrMemoryPoolTests, PooledMemoryBlockReportsLogicalCapacityAndStride)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 10;
            config.blockCount = 1;
            config.alignment = 8;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = pool->AcquireBlock();
            ASSERT_TRUE(acquireResult.Succeeded());
            NrPooledMemoryBlock block = acquireResult.TakeValue();

            EXPECT_EQ(block.Capacity(), 10u);
            EXPECT_EQ(block.Stride(), 16u);
        }

        TEST(NrMemoryPoolTests, CreateFailsWhenBlockSizeIsZero)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 0;
            config.blockCount = 32;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolTests, CreateFailsWhenBlockCountIsZero)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 1024;
            config.blockCount = 0;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolTests, CreateFailsWhenAlignmentIsZero)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 1024;
            config.blockCount = 32;
            config.alignment = 0;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolTests, CreateFailsWhenAlignmentIsNotPowerOfTwo)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 1024;
            config.blockCount = 32;
            config.alignment = 24;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolTests, CreateFailsWhenAlignedBlockSizeWouldOverflow)
        {
            NrMemoryPoolConfig config;
            config.blockSize = std::numeric_limits<std::size_t>::max();
            config.blockCount = 1;
            config.alignment = 8;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::CapacityExceeded);
        }

        TEST(NrMemoryPoolTests, CreateFailsWhenStorageSizeWouldOverflow)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = (std::numeric_limits<std::size_t>::max() / config.blockSize) + 1;
            config.alignment = 8;

            NrResult<std::unique_ptr<NrMemoryPool>> result = NrMemoryPool::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::CapacityExceeded);
        }

        TEST(NrMemoryPoolTests, AcquireReturnsAlignedBlocksAndUpdatesStats)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 2;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<std::byte*> firstAcquire = pool->Acquire();
            NrResult<std::byte*> secondAcquire = pool->Acquire();

            ASSERT_TRUE(firstAcquire.Succeeded());
            ASSERT_TRUE(secondAcquire.Succeeded());

            std::byte* firstBlock = firstAcquire.TakeValue();
            std::byte* secondBlock = secondAcquire.TakeValue();

            EXPECT_NE(firstBlock, nullptr);
            EXPECT_NE(secondBlock, nullptr);
            EXPECT_NE(firstBlock, secondBlock);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(firstBlock) % config.alignment, 0u);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(secondBlock) % config.alignment, 0u);

            const NrMemoryPoolStats stats = pool->Stats();
            EXPECT_EQ(stats.capacity, config.blockCount);
            EXPECT_EQ(stats.inUse, 2u);
            EXPECT_EQ(stats.available, 0u);
            EXPECT_EQ(stats.highWatermark, 2u);
            EXPECT_EQ(stats.failedAcquireCount, 0u);
        }

        TEST(NrMemoryPoolTests, AcquireFailsWhenPoolIsExhausted)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<std::byte*> firstAcquire = pool->Acquire();
            NrResult<std::byte*> secondAcquire = pool->Acquire();

            EXPECT_TRUE(firstAcquire.Succeeded());
            EXPECT_TRUE(secondAcquire.Failed());
            EXPECT_FALSE(secondAcquire.HasValue());
            EXPECT_EQ(secondAcquire.ErrorCode(), NrErrorCode::PoolExhausted);

            const NrMemoryPoolStats stats = pool->Stats();
            EXPECT_EQ(stats.inUse, 1u);
            EXPECT_EQ(stats.available, 0u);
            EXPECT_EQ(stats.highWatermark, 1u);
            EXPECT_EQ(stats.failedAcquireCount, 1u);
        }

        TEST(NrMemoryPoolTests, FailedAcquireCountSaturatesAtUint64Maximum)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<std::byte*> firstAcquire = pool->Acquire();
            ASSERT_TRUE(firstAcquire.Succeeded());

            constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();
            NrMemoryPoolTestAccess::SetFailedAcquireCount(*pool, MaxValue - 1);

            EXPECT_EQ(pool->Acquire().ErrorCode(), NrErrorCode::PoolExhausted);
            EXPECT_EQ(pool->Stats().failedAcquireCount, MaxValue);

            EXPECT_EQ(pool->Acquire().ErrorCode(), NrErrorCode::PoolExhausted);
            EXPECT_EQ(pool->Stats().failedAcquireCount, MaxValue);
        }

        TEST(NrMemoryPoolTests, ReleaseReturnsBlockToPool)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<std::byte*> acquireResult = pool->Acquire();
            ASSERT_TRUE(acquireResult.Succeeded());
            std::byte* block = acquireResult.TakeValue();

            NrStatus releaseStatus = pool->Release(block);

            EXPECT_TRUE(releaseStatus.Succeeded());

            const NrMemoryPoolStats stats = pool->Stats();
            EXPECT_EQ(stats.inUse, 0u);
            EXPECT_EQ(stats.available, 1u);
            EXPECT_EQ(stats.highWatermark, 1u);

            NrResult<std::byte*> reacquireResult = pool->Acquire();
            ASSERT_TRUE(reacquireResult.Succeeded());
            EXPECT_EQ(reacquireResult.TakeValue(), block);
        }

        TEST(NrMemoryPoolTests, ReleaseRejectsInvalidBlocks)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<std::byte*> acquireResult = pool->Acquire();
            ASSERT_TRUE(acquireResult.Succeeded());
            std::byte* block = acquireResult.TakeValue();

            std::byte outsideBlock[16] = {};

            EXPECT_EQ(pool->Release(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(pool->Release(outsideBlock).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(pool->Release(block + 1).ErrorCode(), NrErrorCode::InvalidArgument);

            EXPECT_TRUE(pool->Release(block).Succeeded());
            EXPECT_EQ(pool->Release(block).ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrMemoryPoolTests, PooledMemoryBlockReturnsBlockOnDestruction)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            std::byte* borrowedBlock = nullptr;
            {
                NrResult<NrPooledMemoryBlock> acquireResult = pool->AcquireBlock();
                ASSERT_TRUE(acquireResult.Succeeded());

                NrPooledMemoryBlock block = acquireResult.TakeValue();
                borrowedBlock = block.Data();

                EXPECT_TRUE(block.IsValid());
                EXPECT_NE(borrowedBlock, nullptr);
                EXPECT_EQ(pool->Stats().inUse, 1u);
                EXPECT_EQ(pool->Stats().available, 0u);
            }

            EXPECT_EQ(pool->Stats().inUse, 0u);
            EXPECT_EQ(pool->Stats().available, 1u);

            NrResult<std::byte*> reacquireResult = pool->Acquire();
            ASSERT_TRUE(reacquireResult.Succeeded());
            EXPECT_EQ(reacquireResult.TakeValue(), borrowedBlock);
        }

        TEST(NrMemoryPoolTests, MovingPooledMemoryBlockTransfersReleaseResponsibility)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrPooledMemoryBlock outerBlock;
            std::byte* borrowedBlock = nullptr;
            {
                NrResult<NrPooledMemoryBlock> acquireResult = pool->AcquireBlock();
                ASSERT_TRUE(acquireResult.Succeeded());

                NrPooledMemoryBlock firstBlock = acquireResult.TakeValue();
                borrowedBlock = firstBlock.Data();

                NrPooledMemoryBlock secondBlock(std::move(firstBlock));
                EXPECT_FALSE(firstBlock.IsValid());
                EXPECT_TRUE(secondBlock.IsValid());
                EXPECT_EQ(secondBlock.Data(), borrowedBlock);

                outerBlock = std::move(secondBlock);
                EXPECT_FALSE(secondBlock.IsValid());
                EXPECT_TRUE(outerBlock.IsValid());
            }

            EXPECT_EQ(pool->Stats().inUse, 1u);
            EXPECT_EQ(pool->Stats().available, 0u);

            EXPECT_TRUE(outerBlock.Reset().Succeeded());
            EXPECT_FALSE(outerBlock.IsValid());
            EXPECT_EQ(pool->Stats().inUse, 0u);
            EXPECT_EQ(pool->Stats().available, 1u);
        }

        TEST(NrMemoryPoolTests, AcquireBlockFailsWhenPoolIsExhausted)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 16;
            config.blockCount = 1;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> firstAcquire = pool->AcquireBlock();
            NrResult<NrPooledMemoryBlock> secondAcquire = pool->AcquireBlock();

            EXPECT_TRUE(firstAcquire.Succeeded());
            EXPECT_TRUE(secondAcquire.Failed());
            EXPECT_FALSE(secondAcquire.HasValue());
            EXPECT_EQ(secondAcquire.ErrorCode(), NrErrorCode::PoolExhausted);
            EXPECT_EQ(pool->Stats().failedAcquireCount, 1u);
        }

        TEST(NrMemoryPoolTests, AcquireBlockAndReleaseAreThreadSafeUnderContention)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 64;
            config.blockCount = 8;
            config.alignment = 16;

            NrResult<std::unique_ptr<NrMemoryPool>> createResult = NrMemoryPool::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPool> pool = createResult.TakeValue();

            constexpr std::size_t threadCount = 8;
            constexpr std::size_t iterationCount = 1000;

            std::atomic<bool> start{false};                     // start flag
            std::atomic<std::size_t> successfulAcquireCount{0}; // 성공 횟수
            std::atomic<std::size_t> exhaustedAcquireCount{0};  // 정상적인 경쟁 실패 횟수(pool이 가득 찬 경우)
            std::atomic<std::size_t> unexpectedFailureCount{0}; // 버그 횟수

            std::vector<std::thread> workers;
            workers.reserve(threadCount); // 메모리 공간만 확보. std::thread 는 0개

            for (std::size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
            {
                // std::thread 생성자는 '실행할 함수, 호출 가능한 객체'를 받을 수 있음
                workers.emplace_back(
                    [&pool, &start, &successfulAcquireCount, &exhaustedAcquireCount, &unexpectedFailureCount,
                     threadIndex]()
                    {
                        // start == true 이면
                        // true를 store(release)하기 전에 메인 스레드에서 처리한 작업을 코드에서 보이도록 한다.
                        while (!start.load(std::memory_order_acquire))
                        {
                            std::this_thread::yield(); // 현재 실행 중인 스레드를 yield
                        }

                        for (std::size_t iteration = 0; iteration < iterationCount; ++iteration)
                        {
                            // 풀에서 메모리 블록 acquire
                            NrResult<NrPooledMemoryBlock> acquireResult = pool->AcquireBlock();

                            if (acquireResult.Succeeded())
                            {
                                NrPooledMemoryBlock block = acquireResult.TakeValue();
                                block.Data()[0] = static_cast<std::byte>(threadIndex & 0xFF);
                                successfulAcquireCount.fetch_add(1, std::memory_order_relaxed);
                                continue;
                            }

                            // pool이 꽉참
                            if (acquireResult.ErrorCode() == NrErrorCode::PoolExhausted)
                            {
                                exhaustedAcquireCount.fetch_add(1, std::memory_order_relaxed);
                                std::this_thread::yield();
                                continue;
                            }

                            unexpectedFailureCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    });
            }

            // main thread : 모든 worker thread를 만들고 나서 출발시키기 위한 신호를 true로 설정
            // store 이전에 한 작업들을 acquire로 읽으면 다른 스레드에서도 볼 수 있음
            start.store(true, std::memory_order_release);

            for (std::thread& worker : workers)
            {
                worker.join();
            }

            const NrMemoryPoolStats stats = pool->Stats();

            EXPECT_GT(successfulAcquireCount.load(), 0u);
            EXPECT_EQ(unexpectedFailureCount.load(), 0u);
            EXPECT_EQ(stats.capacity, config.blockCount);
            EXPECT_EQ(stats.inUse, 0u);
            EXPECT_EQ(stats.available, config.blockCount);
            EXPECT_LE(stats.highWatermark, config.blockCount);
            EXPECT_EQ(stats.failedAcquireCount, exhaustedAcquireCount.load());
        }
    } // namespace
} // namespace psnr::core
