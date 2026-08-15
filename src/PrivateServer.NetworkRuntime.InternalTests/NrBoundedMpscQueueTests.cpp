#include "pch.h"

#include "NrBoundedMpscQueue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace psnr::core
{
    namespace
    {
        using NrIntQueue = NrBoundedMpscQueue<int>;
        constexpr NrMemoryPoolRole TestQueueStorageRole = NrMemoryPoolRole::RuntimeIngressQueueStorage;

        struct NrTrackedQueueItem
        {
            explicit NrTrackedQueueItem(int itemValue = 0) noexcept
                : value(itemValue)
            {
            }

            NrTrackedQueueItem(const NrTrackedQueueItem& other) noexcept
                : value(other.value)
            {
                ++copyConstructCount;
            }

            NrTrackedQueueItem(NrTrackedQueueItem&& other) noexcept
                : value(other.value)
            {
                other.movedFrom = true;
                ++moveConstructCount;
            }

            NrTrackedQueueItem& operator=(NrTrackedQueueItem&& other) noexcept
            {
                if (this != &other)
                {
                    value = other.value;
                    other.movedFrom = true;
                    ++moveAssignCount;
                }

                return *this;
            }

            ~NrTrackedQueueItem() noexcept
            {
                ++destructCount;
            }

            static void ResetCounters() noexcept
            {
                copyConstructCount = 0;
                moveConstructCount = 0;
                moveAssignCount = 0;
                destructCount = 0;
            }

            int value = 0;
            bool movedFrom = false;

            inline static std::size_t copyConstructCount = 0;
            inline static std::size_t moveConstructCount = 0;
            inline static std::size_t moveAssignCount = 0;
            inline static std::size_t destructCount = 0;
        };

        using NrTrackedQueue = NrBoundedMpscQueue<NrTrackedQueueItem>;

        [[nodiscard]] NrMemoryPoolConfig MakePoolConfig(std::size_t blockSize, std::size_t blockCount,
                                                        std::size_t alignment)
        {
            NrMemoryPoolConfig config;
            config.blockSize = blockSize;
            config.blockCount = blockCount;
            config.alignment = alignment;
            return config;
        }

        [[nodiscard]] NrMemoryPoolManagerConfig MakeManagerConfig(std::size_t queueBlockSize = 1024,
                                                                  std::size_t queueBlockCount = 1)
        {
            NrMemoryPoolManagerConfig config;
            config.pools = {
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RecvBuffer, MakePoolConfig(64, 1, 16)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SendBuffer, MakePoolConfig(64, 1, 16)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::OverlappedContext, MakePoolConfig(64, 1, 16)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage,
                                              MakePoolConfig(queueBlockSize, queueBlockCount, NrCacheLineSize)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload64, MakePoolConfig(64, 1, 16)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload256, MakePoolConfig(256, 1, 16)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload1024, MakePoolConfig(1024, 1, 16)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload8192, MakePoolConfig(8192, 1, 16)},
            };
            return config;
        }

        [[nodiscard]] std::unique_ptr<NrMemoryPoolManager> CreateManager(
            const NrMemoryPoolManagerConfig& config = MakeManagerConfig())
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult = NrMemoryPoolManager::Create(config);
            EXPECT_TRUE(createResult.Succeeded());
            if (createResult.Failed())
            {
                return nullptr;
            }

            return createResult.TakeValue();
        }

        [[nodiscard]] NrMemoryPoolStats QueueStorageStats(NrMemoryPoolManager& manager)
        {
            NrResult<NrMemoryPoolStats> statsResult = manager.Stats(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            EXPECT_TRUE(statsResult.Succeeded());
            if (statsResult.Failed())
            {
                return NrMemoryPoolStats{};
            }

            return statsResult.TakeValue();
        }

        TEST(NrBoundedMpscQueueTests, QueueIsNotCopyableOrMovable)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrIntQueue>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrIntQueue>);
            EXPECT_FALSE(std::is_move_constructible_v<NrIntQueue>);
            EXPECT_FALSE(std::is_move_assignable_v<NrIntQueue>);
        }

        TEST(NrBoundedMpscQueueTests, CreateWithValidConfigAcquiresQueueStorageBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 8);

            ASSERT_TRUE(createResult.Succeeded());
            ASSERT_TRUE(createResult.HasValue());

            std::unique_ptr<NrIntQueue> queue = createResult.TakeValue();
            ASSERT_NE(queue, nullptr);

            EXPECT_EQ(queue->Capacity(), 8u);
            EXPECT_EQ(queue->SizeApprox(), 0u);

            const NrMemoryPoolStats queueStats = QueueStorageStats(*manager);
            EXPECT_EQ(queueStats.inUse, 1u);
            EXPECT_EQ(queueStats.available, 0u);
        }

        TEST(NrBoundedMpscQueueTests, CreateUsesTheRequestedStorageRole)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, NrMemoryPoolRole::PayloadRefControl, 8);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(QueueStorageStats(*manager).inUse, 0u);
        }

        TEST(NrBoundedMpscQueueTests, CreateFailsWhenCapacityIsZero)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 0);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(QueueStorageStats(*manager).inUse, 0u);
        }

        TEST(NrBoundedMpscQueueTests, CreateFailsWhenCapacityIsOne)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 1);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(QueueStorageStats(*manager).inUse, 0u);
        }

        TEST(NrBoundedMpscQueueTests, CreateFailsWhenCapacityIsNotPowerOfTwo)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 3);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(QueueStorageStats(*manager).inUse, 0u);
        }

        TEST(NrBoundedMpscQueueTests, CreateFailsWhenQueueStorageBlockIsTooSmall)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager(MakeManagerConfig(64));
            ASSERT_NE(manager, nullptr);

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 2);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::CapacityExceeded);
            EXPECT_EQ(QueueStorageStats(*manager).inUse, 0u);
        }

        TEST(NrBoundedMpscQueueTests, CreateFailsWhenQueueStoragePoolIsExhausted)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<NrPooledMemoryBlock> reservedBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            ASSERT_TRUE(reservedBlockResult.Succeeded());
            NrPooledMemoryBlock reservedBlock = reservedBlockResult.TakeValue();

            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 8);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::PoolExhausted);
        }

        TEST(NrBoundedMpscQueueTests, DestroyReturnsQueueStorageBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            {
                NrResult<std::unique_ptr<NrIntQueue>> createResult =
                    NrIntQueue::Create(*manager, TestQueueStorageRole, 8);
                ASSERT_TRUE(createResult.Succeeded());
                std::unique_ptr<NrIntQueue> queue = createResult.TakeValue();

                EXPECT_EQ(QueueStorageStats(*manager).inUse, 1u);
            }

            const NrMemoryPoolStats queueStats = QueueStorageStats(*manager);
            EXPECT_EQ(queueStats.inUse, 0u);
            EXPECT_EQ(queueStats.available, 1u);
        }

        TEST(NrBoundedMpscQueueTests, TryPopFromEmptyQueueReturnsQueueEmptyAndKeepsOutput)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrIntQueue> queue = createResult.TakeValue();

            int output = 123;

            const NrStatus status = queue->TryPop(output);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::QueueEmpty);
            EXPECT_EQ(output, 123);
            EXPECT_EQ(queue->SizeApprox(), 0u);
        }

        TEST(NrBoundedMpscQueueTests, TryPushAndTryPopMoveItemThroughQueue)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrIntQueue> queue = createResult.TakeValue();

            EXPECT_TRUE(queue->TryPush(42).Succeeded());
            EXPECT_EQ(queue->SizeApprox(), 1u);

            int output = 0;
            EXPECT_TRUE(queue->TryPop(output).Succeeded());

            EXPECT_EQ(output, 42);
            EXPECT_EQ(queue->SizeApprox(), 0u);
        }

        TEST(NrBoundedMpscQueueTests, TryPushToFullQueueReturnsQueueFullAndDoesNotCopyOrMoveInput)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrTrackedQueue>> createResult =
                NrTrackedQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrTrackedQueue> queue = createResult.TakeValue();

            NrTrackedQueueItem firstItem(1);
            ASSERT_TRUE(queue->TryPush(firstItem).Succeeded());
            NrTrackedQueueItem secondItem(2);
            ASSERT_TRUE(queue->TryPush(secondItem).Succeeded());

            NrTrackedQueueItem blockedItem(3);
            NrTrackedQueueItem::ResetCounters();

            const NrStatus status = queue->TryPush(blockedItem);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::QueueFull);
            EXPECT_EQ(NrTrackedQueueItem::copyConstructCount, 0u);
            EXPECT_EQ(NrTrackedQueueItem::moveConstructCount, 0u);
            EXPECT_FALSE(blockedItem.movedFrom);
        }

        TEST(NrBoundedMpscQueueTests, TryPushCopiesLvalueIntoQueueSlot)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrTrackedQueue>> createResult =
                NrTrackedQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrTrackedQueue> queue = createResult.TakeValue();

            NrTrackedQueueItem item(7);
            NrTrackedQueueItem::ResetCounters();

            EXPECT_TRUE(queue->TryPush(item).Succeeded());

            EXPECT_EQ(NrTrackedQueueItem::copyConstructCount, 1u);
            EXPECT_EQ(NrTrackedQueueItem::moveConstructCount, 0u);
            EXPECT_FALSE(item.movedFrom);
        }

        TEST(NrBoundedMpscQueueTests, TryPushMovesRvalueIntoQueueSlot)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrTrackedQueue>> createResult =
                NrTrackedQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrTrackedQueue> queue = createResult.TakeValue();

            NrTrackedQueueItem item(9);
            NrTrackedQueueItem::ResetCounters();

            EXPECT_TRUE(queue->TryPush(std::move(item)).Succeeded());

            EXPECT_EQ(NrTrackedQueueItem::copyConstructCount, 0u);
            EXPECT_EQ(NrTrackedQueueItem::moveConstructCount, 1u);
            EXPECT_TRUE(item.movedFrom);
        }

        TEST(NrBoundedMpscQueueTests, TryPopMoveAssignsOutputAndDestroysQueueSlotItem)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrTrackedQueue>> createResult =
                NrTrackedQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrTrackedQueue> queue = createResult.TakeValue();

            ASSERT_TRUE(queue->TryPush(NrTrackedQueueItem(11)).Succeeded());
            NrTrackedQueueItem output;
            NrTrackedQueueItem::ResetCounters();

            EXPECT_TRUE(queue->TryPop(output).Succeeded());

            EXPECT_EQ(output.value, 11);
            EXPECT_EQ(NrTrackedQueueItem::moveAssignCount, 1u);
            EXPECT_EQ(NrTrackedQueueItem::destructCount, 1u);
        }

        TEST(NrBoundedMpscQueueTests, DestructorDestroysRemainingQueueSlotItems)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrTrackedQueue>> createResult =
                NrTrackedQueue::Create(*manager, TestQueueStorageRole, 2);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrTrackedQueue> queue = createResult.TakeValue();

            ASSERT_TRUE(queue->TryPush(NrTrackedQueueItem(1)).Succeeded());
            ASSERT_TRUE(queue->TryPush(NrTrackedQueueItem(2)).Succeeded());
            NrTrackedQueueItem::ResetCounters();

            queue.reset();

            EXPECT_EQ(NrTrackedQueueItem::destructCount, 2u);
            EXPECT_EQ(QueueStorageStats(*manager).inUse, 0u);
        }

        TEST(NrBoundedMpscQueueTests, MultiProducerSingleConsumerStressConsumesEveryItemOnce)
        {
            constexpr std::size_t ProducerCount = 4;
            constexpr std::size_t ItemsPerProducer = 1000;
            constexpr std::size_t TotalItemCount = ProducerCount * ItemsPerProducer;
            constexpr std::size_t QueueCapacity = 64;
            constexpr std::size_t QueueStorageBytes = QueueCapacity * NrCacheLineSize;

            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager(MakeManagerConfig(QueueStorageBytes));
            ASSERT_NE(manager, nullptr);
            NrResult<std::unique_ptr<NrIntQueue>> createResult =
                NrIntQueue::Create(*manager, TestQueueStorageRole, QueueCapacity);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrIntQueue> queue = createResult.TakeValue();

            std::atomic<bool> start{false};
            std::atomic<std::size_t> readyProducerCount{0};
            std::atomic<std::size_t> finishedProducerCount{0};
            std::atomic<std::size_t> unexpectedPushFailureCount{0};

            std::vector<std::thread> producers;
            producers.reserve(ProducerCount);

            for (std::size_t producerIndex = 0; producerIndex < ProducerCount; ++producerIndex)
            {
                producers.emplace_back(
                    [&queue, &start, &readyProducerCount, &finishedProducerCount, &unexpectedPushFailureCount,
                     producerIndex]()
                    {
                        readyProducerCount.fetch_add(1, std::memory_order_relaxed);
                        while (!start.load(std::memory_order_acquire))
                        {
                            std::this_thread::yield();
                        }

                        const int baseValue = static_cast<int>(producerIndex * ItemsPerProducer);
                        // ItemsPerProducer 개수만큼 각 thread 별로 push
                        for (std::size_t itemIndex = 0; itemIndex < ItemsPerProducer; ++itemIndex)
                        {
                            const int value = baseValue + static_cast<int>(itemIndex);

                            while (true)
                            {
                                const NrStatus pushStatus = queue->TryPush(value);
                                if (pushStatus.Succeeded())
                                {
                                    break;
                                }

                                if (pushStatus.ErrorCode() != NrErrorCode::QueueFull)
                                {
                                    unexpectedPushFailureCount.fetch_add(1, std::memory_order_relaxed);
                                    finishedProducerCount.fetch_add(1, std::memory_order_release);
                                    return;
                                }

                                std::this_thread::yield();
                            }
                        }

                        finishedProducerCount.fetch_add(1, std::memory_order_release);
                    });
            }

            while (readyProducerCount.load(std::memory_order_relaxed) < ProducerCount) // producer thread 대기
            {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release); // producer 작업 시작

            std::vector<std::size_t> seenCounts(TotalItemCount, 0);
            std::size_t consumedCount = 0;
            std::size_t unexpectedPopFailureCount = 0;
            std::size_t outOfRangeValueCount = 0;
            bool drainAfterProducersFinished = false;
            std::chrono::steady_clock::time_point drainUntil{};

            while (consumedCount < TotalItemCount)
            {
                int value = -1;
                const NrStatus popStatus = queue->TryPop(value);

                if (popStatus.Succeeded())
                {
                    if (value >= 0 && static_cast<std::size_t>(value) < seenCounts.size())
                    {
                        ++seenCounts[static_cast<std::size_t>(value)];
                    }
                    else
                    {
                        ++outOfRangeValueCount;
                    }

                    ++consumedCount;
                    continue;
                }

                if (popStatus.ErrorCode() != NrErrorCode::QueueEmpty)
                {
                    ++unexpectedPopFailureCount;
                    break;
                }

                if (finishedProducerCount.load(std::memory_order_acquire) == ProducerCount)
                {
                    // 이 테스트는 raw MPSC queue만 stress하고 schedule gate/notify 구조를 사용하지 않는다.
                    // QueueEmpty는 "이 순간 consumer가 pop 가능한 slot이 없다"는 관찰일 뿐이다.
                    // producer가 slot을 예약했지만 아직 sequence store로 publish하지 않은 구간에서는
                    // consumer가 QueueEmpty를 정상적으로 볼 수 있고, 직후 마지막 item이 publish될 수 있다.
                    // 실제 runtime actor 경로는 admission transaction 안에서 mailbox commit 후 ready token을 게시하고,
                    // schedule gate가 consumer에게 mailbox를 다시 확인할 run 기회를 보장한다.
                    // 따라서 product code에서는 TryPop은 실패할 수 있으며, producer notify 를 통해 재확인해야 한다.
                    // 테스트에서는 producer 완료 후에도 짧게 drain을 재시도한 뒤에만 더 이상 item이 없다고 판단한다.
                    if (!drainAfterProducersFinished)
                    {
                        drainAfterProducersFinished = true;
                        drainUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                    }

                    if (std::chrono::steady_clock::now() >= drainUntil)
                    {
                        break;
                    }
                }

                if (unexpectedPushFailureCount.load(std::memory_order_relaxed) > 0)
                {
                    break;
                }

                std::this_thread::yield();
            }

            for (std::thread& producer : producers)
            {
                producer.join(); // producer 종료
            }

            EXPECT_EQ(unexpectedPushFailureCount.load(std::memory_order_relaxed), 0u);
            EXPECT_EQ(unexpectedPopFailureCount, 0u);
            EXPECT_EQ(outOfRangeValueCount, 0u);
            EXPECT_EQ(consumedCount, TotalItemCount);

            for (std::size_t value = 0; value < seenCounts.size(); ++value)
            {
                // 정확히 1 번 consume 되었는지 확인
                EXPECT_EQ(seenCounts[value], 1u) << "value=" << value;
            }
        }
    } // namespace
} // namespace psnr::core
