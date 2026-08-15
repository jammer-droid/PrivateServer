#include "pch.h"

#include "NrBoundedMpscQueue.h"
#include "NrDiagnosticEmitter.h"
#include "NrDiagnosticsComponent.h"
#include "NrDiagnosticsConfigInternal.h"
#include "NrDiagnosticSink.h"
#include "NrMemoryPoolManager.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace psnr::runtime::internal
{
    namespace
    {
        using NrDiagnosticsQueue = psnr::core::NrBoundedMpscQueue<NrDiagnosticRecord>;

        struct NrEmitterTestSinkState final
        {
            std::atomic<std::uint64_t> beginCount{0};
            std::atomic<std::uint64_t> consumeCount{0};
            std::atomic<std::uint64_t> finishCount{0};
        };

        class NrEmitterTestSink final : public INrDiagnosticSink
        {
        public:
            explicit NrEmitterTestSink(std::shared_ptr<NrEmitterTestSinkState> state) noexcept
                : state_(std::move(state))
            {
            }

            [[nodiscard]] psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata&) noexcept override
            {
                state_->beginCount.fetch_add(1, std::memory_order_relaxed);
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Consume(const NrDiagnosticRecord&) noexcept override
            {
                state_->consumeCount.fetch_add(1, std::memory_order_relaxed);
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Finish(const NrDiagnosticSummary&) noexcept override
            {
                state_->finishCount.fetch_add(1, std::memory_order_relaxed);
                return psnr::core::NrStatus::Success();
            }

        private:
            std::shared_ptr<NrEmitterTestSinkState> state_;
        };

        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateEmitterTestPoolManager()
        {
            const psnr::core::NrResult<std::size_t> storageBytesResult =
                NrDiagnosticsQueue::RequiredStorageBytes(NrDiagnosticsConfigInternal::QueueCapacity);
            if (storageBytesResult.Failed())
            {
                return nullptr;
            }

            psnr::core::NrMemoryPoolManagerConfig config;
            config.pools = {
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                    psnr::core::NrMemoryPoolConfig{
                        storageBytesResult.Value(),
                        1,
                        psnr::core::NrCacheLineSize,
                    },
                },
            };

            psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
                psnr::core::NrMemoryPoolManager::Create(config);
            return managerResult.Succeeded() ? managerResult.TakeValue() : nullptr;
        }

        TEST(NrDiagnosticEmitterTests, DefaultEmitterIsDisabledNoOpValue)
        {
            static_assert(std::is_nothrow_default_constructible_v<NrDiagnosticEmitter>);
            static_assert(std::is_nothrow_copy_constructible_v<NrDiagnosticEmitter>);
            static_assert(std::is_nothrow_copy_assignable_v<NrDiagnosticEmitter>);
            static_assert(std::is_nothrow_move_constructible_v<NrDiagnosticEmitter>);
            static_assert(std::is_nothrow_move_assignable_v<NrDiagnosticEmitter>);
            static_assert(std::is_trivially_copyable_v<NrDiagnosticEmitter>);
            static_assert(sizeof(NrDiagnosticEmitter) == sizeof(void*));
            static_assert(noexcept(std::declval<const NrDiagnosticEmitter&>().Emit(NrDiagnosticRecord{})));

            const NrDiagnosticEmitter emitter;
            EXPECT_FALSE(emitter.IsEnabled());

            NrDiagnosticRecord record;
            record.sessionKey = 42;
            emitter.Emit(record);

            EXPECT_EQ(record.sessionKey, 42u);
            EXPECT_EQ(record.producerTimestamp, 0u);
            EXPECT_EQ(record.drainSequence, 0u);
        }

        TEST(NrDiagnosticEmitterTests, CopiedBorrowedHandleBecomesDisabledAfterComponentShutdown)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateEmitterTestPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrEmitterTestSinkState> sinkState = std::make_shared<NrEmitterTestSinkState>();
            std::unique_ptr<INrDiagnosticSink> sink = std::make_unique<NrEmitterTestSink>(sinkState);
            const NrDiagnosticsConfigInternal config{NrDiagnosticsMode::Debug, {}};

            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> componentResult =
                NrDiagnosticsComponent::Create(config, *manager, std::move(sink));
            ASSERT_TRUE(componentResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> component = componentResult.TakeValue();
            ASSERT_NE(component, nullptr);

            NrDiagnosticEmitter emitter = component->Emitter();
            NrDiagnosticEmitter copiedEmitter = emitter;
            EXPECT_FALSE(emitter.IsEnabled());
            EXPECT_FALSE(copiedEmitter.IsEnabled());

            NrBootstrapContext bootstrapContext;
            ASSERT_TRUE(component->Configure(bootstrapContext).Succeeded());
            ASSERT_TRUE(component->Start().Succeeded());
            EXPECT_TRUE(emitter.IsEnabled());
            EXPECT_TRUE(copiedEmitter.IsEnabled());

            NrDiagnosticRecord record;
            record.sessionKey = 77;
            copiedEmitter.Emit(record);

            ASSERT_TRUE(component->Shutdown().Succeeded());
            EXPECT_FALSE(emitter.IsEnabled());
            EXPECT_FALSE(copiedEmitter.IsEnabled());

            const NrDiagnosticsStats shutdownStats = component->CaptureStats();
            EXPECT_EQ(shutdownStats.attempted, 1u);
            EXPECT_EQ(shutdownStats.enqueued, 1u);
            EXPECT_EQ(shutdownStats.consumed, 1u);
            EXPECT_EQ(shutdownStats.droppedQueueFull, 0u);
            EXPECT_EQ(shutdownStats.droppedSinkUnavailable, 0u);
            EXPECT_EQ(shutdownStats.discardedAfterSinkFailure, 0u);
            EXPECT_EQ(sinkState->consumeCount.load(std::memory_order_relaxed), 1u);
            EXPECT_EQ(sinkState->finishCount.load(std::memory_order_relaxed), 1u);

            const psnr::core::NrResult<psnr::core::NrMemoryPoolStats> poolStatsResult =
                manager->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(poolStatsResult.Succeeded());
            EXPECT_EQ(poolStatsResult.Value().inUse, 0u);

            emitter.Emit(NrDiagnosticRecord{});
            copiedEmitter.Emit(NrDiagnosticRecord{});
            EXPECT_EQ(component->CaptureStats().attempted, shutdownStats.attempted);
            EXPECT_EQ(sinkState->consumeCount.load(std::memory_order_relaxed), 1u);
        }

        TEST(NrDiagnosticEmitterTests, ConcurrentShutdownQuiescesAdmittedEmitterCopies)
        {
            constexpr std::size_t ProducerCount = 4;
            constexpr std::size_t AttemptsPerProducer = 2048;

            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateEmitterTestPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrEmitterTestSinkState> sinkState = std::make_shared<NrEmitterTestSinkState>();
            const NrDiagnosticsConfigInternal config{NrDiagnosticsMode::Debug, {}};
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> componentResult =
                NrDiagnosticsComponent::Create(config, *manager, std::make_unique<NrEmitterTestSink>(sinkState));
            ASSERT_TRUE(componentResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> component = componentResult.TakeValue();

            NrBootstrapContext bootstrapContext;
            ASSERT_TRUE(component->Configure(bootstrapContext).Succeeded());
            ASSERT_TRUE(component->Start().Succeeded());
            const NrDiagnosticEmitter emitter = component->Emitter();

            std::atomic<std::size_t> readyProducerCount{0};
            std::atomic_bool startProducers{false};
            std::vector<std::thread> producers;
            producers.reserve(ProducerCount);
            for (std::size_t producerIndex = 0; producerIndex < ProducerCount; ++producerIndex)
            {
                producers.emplace_back(
                    [emitter, &readyProducerCount, &startProducers, producerIndex]() noexcept
                    {
                        readyProducerCount.fetch_add(1, std::memory_order_release);
                        startProducers.wait(false, std::memory_order_acquire);

                        for (std::size_t attempt = 0; attempt < AttemptsPerProducer; ++attempt)
                        {
                            NrDiagnosticRecord record;
                            record.sessionKey = static_cast<psnr::core::NrSessionKey>(
                                producerIndex * AttemptsPerProducer + attempt + 1);
                            emitter.Emit(record);
                        }
                    });
            }

            while (readyProducerCount.load(std::memory_order_acquire) != ProducerCount)
            {
                std::this_thread::yield();
            }
            startProducers.store(true, std::memory_order_release);
            startProducers.notify_all();

            while (component->CaptureStats().attempted == 0)
            {
                std::this_thread::yield();
            }

            ASSERT_TRUE(component->Shutdown().Succeeded());
            for (std::thread& producer : producers)
            {
                producer.join();
            }

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_GT(stats.attempted, 0u);
            EXPECT_EQ(stats.attempted, stats.enqueued + stats.droppedQueueFull + stats.droppedSinkUnavailable);
            EXPECT_EQ(stats.enqueued, stats.consumed + stats.discardedAfterSinkFailure);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 0u);
            EXPECT_EQ(sinkState->consumeCount.load(std::memory_order_relaxed), stats.consumed);
            EXPECT_EQ(sinkState->finishCount.load(std::memory_order_relaxed), 1u);
            EXPECT_FALSE(emitter.IsEnabled());
        }
    } // namespace
} // namespace psnr::runtime::internal
