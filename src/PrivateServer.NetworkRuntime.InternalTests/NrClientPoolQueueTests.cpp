#include "pch.h"

#include "NrClientMemoryPoolConfigFactory.h"
#include "NrClientPendingSend.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"

#include "NrBoundedMpscQueue.h"
#include "NrMemoryPoolManager.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>

namespace psnr::runtime::internal
{
    namespace
    {
        using NrIntClientQueue = psnr::core::NrBoundedMpscQueue<int>;

        [[nodiscard]] const psnr::core::NrMemoryPoolConfig& PoolConfig(
            const psnr::core::NrMemoryPoolManagerConfig& config, psnr::core::NrMemoryPoolRole role)
        {
            return config.pools[static_cast<std::size_t>(role)].pool;
        }

        [[nodiscard]] psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig> MakeClientConfig(
            std::size_t eventCapacity, std::size_t payloadQueueCapacity)
        {
            psnr::core::NrResult<std::size_t> eventStorageResult =
                NrIntClientQueue::RequiredStorageBytes(eventCapacity);
            if (eventStorageResult.Failed())
            {
                return psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig>::Failure(
                    eventStorageResult.Status());
            }

            psnr::core::NrResult<std::size_t> payloadQueueStorageResult =
                NrIntClientQueue::RequiredStorageBytes(payloadQueueCapacity);
            if (payloadQueueStorageResult.Failed())
            {
                return psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig>::Failure(
                    payloadQueueStorageResult.Status());
            }

            NrClientMemoryPoolSizing sizing;
            sizing.eventQueueStorageBytes = eventStorageResult.Value();
            sizing.payloadQueueStorageBytes = payloadQueueStorageResult.Value();
            sizing.payloadQueueCapacity = payloadQueueCapacity;
            return NrClientMemoryPoolConfigFactory::Create(sizing);
        }

        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateClientManager(
            std::size_t eventCapacity, std::size_t payloadQueueCapacity)
        {
            psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig> configResult =
                MakeClientConfig(eventCapacity, payloadQueueCapacity);
            EXPECT_TRUE(configResult.Succeeded());
            if (configResult.Failed())
            {
                return nullptr;
            }

            psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
                psnr::core::NrMemoryPoolManager::Create(configResult.Value());
            EXPECT_TRUE(managerResult.Succeeded());
            if (managerResult.Failed())
            {
                return nullptr;
            }

            return managerResult.TakeValue();
        }

        TEST(NrClientPoolQueueTests, QueueUsesConfiguredMpscCapacityDirectly)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateClientManager(4, 8);
            ASSERT_NE(manager, nullptr);

            psnr::core::NrResult<std::unique_ptr<NrIntClientQueue>> queueResult = NrIntClientQueue::Create(
                *manager, psnr::core::NrMemoryPoolRole::ClientEventQueueStorage, 4);
            ASSERT_TRUE(queueResult.Succeeded());
            std::unique_ptr<NrIntClientQueue> queue = queueResult.TakeValue();

            EXPECT_EQ(queue->Capacity(), 4u);
            EXPECT_EQ(queue->SizeApprox(), 0u);
        }

        TEST(NrClientPoolQueueTests, PendingSendIsMoveOnlyQueueValue)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrClientPendingSend>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrClientPendingSend>);
            EXPECT_TRUE(std::is_nothrow_move_constructible_v<NrClientPendingSend>);
            EXPECT_TRUE(std::is_nothrow_move_assignable_v<NrClientPendingSend>);
        }

        TEST(NrClientPoolQueueTests, QueueRejectsCapacityOutsideMpscRequirements)
        {
            EXPECT_EQ(NrIntClientQueue::RequiredStorageBytes(0).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(NrIntClientQueue::RequiredStorageBytes(1).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(NrIntClientQueue::RequiredStorageBytes(3).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
        }

        TEST(NrClientPoolQueueTests, DirectQueuePushAndPopPreserveConfiguredBound)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateClientManager(4, 4);
            ASSERT_NE(manager, nullptr);
            psnr::core::NrResult<std::unique_ptr<NrIntClientQueue>> queueResult = NrIntClientQueue::Create(
                *manager, psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage, 4);
            ASSERT_TRUE(queueResult.Succeeded());
            std::unique_ptr<NrIntClientQueue> queue = queueResult.TakeValue();

            ASSERT_TRUE(queue->TryPush(1).Succeeded());
            ASSERT_TRUE(queue->TryPush(2).Succeeded());
            ASSERT_TRUE(queue->TryPush(3).Succeeded());
            ASSERT_TRUE(queue->TryPush(4).Succeeded());
            EXPECT_EQ(queue->TryPush(5).ErrorCode(), psnr::core::NrErrorCode::QueueFull);

            int output = 0;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output, 1);
            EXPECT_EQ(queue->SizeApprox(), 3u);
        }

        TEST(NrClientPoolQueueTests, FactorySizesEveryPayloadPoolToPayloadQueueCapacity)
        {
            constexpr std::size_t PayloadQueueCapacity = 4;
            psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig> configResult =
                MakeClientConfig(4, PayloadQueueCapacity);
            ASSERT_TRUE(configResult.Succeeded());
            const psnr::core::NrMemoryPoolManagerConfig& config = configResult.Value();

            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::Payload64).blockCount, 4u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::Payload256).blockCount, 4u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::Payload1024).blockCount, 4u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::Payload8192).blockCount, 4u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::PayloadRefControl).blockCount, 4u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::ClientEventQueueStorage).blockCount, 1u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage).blockCount, 1u);
            EXPECT_EQ(PoolConfig(config, psnr::core::NrMemoryPoolRole::OverlappedContext).blockCount, 3u);
            EXPECT_GE(PoolConfig(config, psnr::core::NrMemoryPoolRole::OverlappedContext).blockSize,
                      sizeof(NrRecvIoContext));
            EXPECT_GE(PoolConfig(config, psnr::core::NrMemoryPoolRole::OverlappedContext).blockSize,
                      sizeof(NrSendIoContext));
        }

        TEST(NrClientPoolQueueTests, FactoryRejectsZeroAndOverflowSizing)
        {
            NrClientMemoryPoolSizing zeroSizing;
            EXPECT_EQ(NrClientMemoryPoolConfigFactory::Create(zeroSizing).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);

            NrClientMemoryPoolSizing overflowSizing;
            overflowSizing.eventQueueStorageBytes = 128;
            overflowSizing.payloadQueueStorageBytes = 128;
            overflowSizing.payloadQueueCapacity = std::numeric_limits<std::size_t>::max();
            EXPECT_EQ(NrClientMemoryPoolConfigFactory::Create(overflowSizing).ErrorCode(),
                      psnr::core::NrErrorCode::CapacityExceeded);

            constexpr std::size_t OverflowCapacity = std::numeric_limits<std::size_t>::max() / 2 + 1;
            EXPECT_EQ(NrIntClientQueue::RequiredStorageBytes(OverflowCapacity).ErrorCode(),
                      psnr::core::NrErrorCode::CapacityExceeded);
        }

    } // namespace
} // namespace psnr::runtime::internal
