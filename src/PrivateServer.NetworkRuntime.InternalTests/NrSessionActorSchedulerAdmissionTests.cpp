#include "pch.h"

#include "NrSessionActorScheduler.h"

#include "NrMemoryPoolTestUtils.h"
#include "NrServerMetrics.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace psnr::runtime
{
    namespace
    {
        TEST(NrSessionActorSchedulerAdmissionTests, PermitCounterStartsEmpty)
        {
            NrRunnableActorPermitCounter permits(2);

            EXPECT_EQ(permits.Capacity(), 2);
            EXPECT_EQ(permits.Count(), 0);
        }

        TEST(NrSessionActorSchedulerAdmissionTests, PermitCounterRejectsAdmissionAtCapacity)
        {
            NrRunnableActorPermitCounter permits(2);

            EXPECT_TRUE(permits.TryAcquire());
            EXPECT_TRUE(permits.TryAcquire());
            EXPECT_FALSE(permits.TryAcquire());
            EXPECT_EQ(permits.Count(), 2);
        }

        TEST(NrSessionActorSchedulerAdmissionTests, ReleasedPermitCanBeAcquiredAgain)
        {
            NrRunnableActorPermitCounter permits(1);
            ASSERT_TRUE(permits.TryAcquire());

            permits.Release();

            EXPECT_EQ(permits.Count(), 0);
            EXPECT_TRUE(permits.TryAcquire());
            EXPECT_EQ(permits.Count(), 1);
        }

        TEST(NrSessionActorSchedulerAdmissionTests, ConcurrentAdmissionsNeverExceedCapacity)
        {
            constexpr std::size_t capacity = 2;
            constexpr std::size_t producerCount = 8;

            NrRunnableActorPermitCounter permits(capacity);
            std::atomic_size_t admittedCount{0};
            std::vector<std::thread> producers;
            producers.reserve(producerCount);

            for (std::size_t index = 0; index < producerCount; ++index)
            {
                producers.emplace_back(
                    [&permits, &admittedCount]() noexcept
                    {
                        if (permits.TryAcquire())
                        {
                            admittedCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    });
            }

            for (std::thread& producer : producers)
            {
                producer.join();
            }

            EXPECT_EQ(admittedCount.load(std::memory_order_relaxed), capacity);
            EXPECT_EQ(permits.Count(), capacity);
        }

        TEST(NrSessionActorSchedulerAdmissionTests, ReadyQueuesUseInjectedMemoryPool)
        {
            constexpr std::size_t workerCount = 2;
            constexpr std::size_t readyQueueCapacity = 4;

            const psnr::core::NrResult<std::size_t> storageBytesResult =
                NrBoundedMpscQueue<NrSessionKey>::RequiredStorageBytes(readyQueueCapacity);
            ASSERT_TRUE(storageBytesResult.Succeeded());

            std::unique_ptr<NrMemoryPoolManager> memoryPoolManager = psnr::core::test::CreateMemoryPoolManager(
                psnr::core::test::MakeMemoryPoolManagerConfigWith(
                    psnr::core::NrMemoryPoolRole::ActorReadyQueueStorage,
                    psnr::core::test::MakePoolConfig(storageBytesResult.Value(), workerCount,
                                                     psnr::core::NrCacheLineSize)));
            ASSERT_NE(memoryPoolManager, nullptr);

            NrSessionActorRegistry registry(readyQueueCapacity);
            {
                NrSessionActorSchedulerConfig config;
                config.actorWorkerCount = workerCount;
                config.maxAdmittedRunnableActors = readyQueueCapacity;

                internal::NrServerMetrics metrics;
                NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics, config);
                NrBootstrapContext bootstrapContext;
                ASSERT_TRUE(scheduler.Configure(bootstrapContext).Succeeded());

                const psnr::core::NrMemoryPoolStats stats = psnr::core::test::Stats(
                    *memoryPoolManager, psnr::core::NrMemoryPoolRole::ActorReadyQueueStorage);
                EXPECT_EQ(stats.inUse, workerCount);
                EXPECT_EQ(stats.available, 0u);
            }

            const psnr::core::NrMemoryPoolStats releasedStats = psnr::core::test::Stats(
                *memoryPoolManager, psnr::core::NrMemoryPoolRole::ActorReadyQueueStorage);
            EXPECT_EQ(releasedStats.inUse, 0u);
            EXPECT_EQ(releasedStats.available, workerCount);
        }
    } // namespace
} // namespace psnr::runtime
