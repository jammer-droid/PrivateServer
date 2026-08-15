#include "pch.h"

#include "NrMemoryPoolManager.h"

#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace psnr::core
{
    namespace
    {
        [[nodiscard]] NrMemoryPoolConfig MakePoolConfig(std::size_t blockCount)
        {
            NrMemoryPoolConfig config;
            config.blockSize = 64;
            config.blockCount = blockCount;
            config.alignment = 16;
            return config;
        }

        [[nodiscard]] NrMemoryPoolManagerConfig MakeManagerConfig()
        {
            NrMemoryPoolManagerConfig config;
            config.pools = {
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RecvBuffer, MakePoolConfig(1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SendBuffer, MakePoolConfig(2)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::OverlappedContext, MakePoolConfig(3)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage, MakePoolConfig(4)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload64, MakePoolConfig(5)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload256, MakePoolConfig(6)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload1024, MakePoolConfig(7)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload8192, MakePoolConfig(8)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::PayloadRefControl, MakePoolConfig(9)},
            };
            return config;
        }

        [[nodiscard]] const NrMemoryPoolStats& FindStats(const NrMemoryPoolManagerStats& stats, NrMemoryPoolRole role)
        {
            for (const NrMemoryPoolManagerStatsEntry& entry : stats.pools)
            {
                if (entry.role == role)
                {
                    return entry.stats;
                }
            }

            ADD_FAILURE() << "Expected memory pool role was not found in manager stats.";
            return stats.pools[0].stats;
        }

        TEST(NrMemoryPoolManagerTests, MemoryPoolManagerIsNotCopyableOrMovable)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrMemoryPoolManager>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrMemoryPoolManager>);
            EXPECT_FALSE(std::is_move_constructible_v<NrMemoryPoolManager>);
            EXPECT_FALSE(std::is_move_assignable_v<NrMemoryPoolManager>);
        }

        TEST(NrMemoryPoolManagerTests, CreateWithValidConfigSucceeds)
        {
            NrMemoryPoolManagerConfig config = MakeManagerConfig();

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            ASSERT_TRUE(result.Succeeded());
            ASSERT_TRUE(result.HasValue());

            std::unique_ptr<NrMemoryPoolManager> manager = result.TakeValue();
            ASSERT_NE(manager, nullptr);

            const NrMemoryPoolManagerStats stats = manager->Stats();
            const NrMemoryPoolStats& recvStats = FindStats(stats, NrMemoryPoolRole::RecvBuffer);
            const NrMemoryPoolStats& sendStats = FindStats(stats, NrMemoryPoolRole::SendBuffer);
            const NrMemoryPoolStats& contextStats = FindStats(stats, NrMemoryPoolRole::OverlappedContext);
            const NrMemoryPoolStats& queueStats = FindStats(stats, NrMemoryPoolRole::RuntimeIngressQueueStorage);
            const NrMemoryPoolStats& payload64Stats = FindStats(stats, NrMemoryPoolRole::Payload64);
            const NrMemoryPoolStats& payload256Stats = FindStats(stats, NrMemoryPoolRole::Payload256);
            const NrMemoryPoolStats& payload1024Stats = FindStats(stats, NrMemoryPoolRole::Payload1024);
            const NrMemoryPoolStats& payload8192Stats = FindStats(stats, NrMemoryPoolRole::Payload8192);
            const NrMemoryPoolStats& payloadRefControlStats = FindStats(stats, NrMemoryPoolRole::PayloadRefControl);

            EXPECT_EQ(recvStats.capacity, config.pools[0].pool.blockCount);
            EXPECT_EQ(recvStats.inUse, 0u);
            EXPECT_EQ(sendStats.capacity, config.pools[1].pool.blockCount);
            EXPECT_EQ(sendStats.inUse, 0u);
            EXPECT_EQ(contextStats.capacity, config.pools[2].pool.blockCount);
            EXPECT_EQ(contextStats.inUse, 0u);
            EXPECT_EQ(queueStats.capacity, config.pools[3].pool.blockCount);
            EXPECT_EQ(queueStats.inUse, 0u);
            EXPECT_EQ(payload64Stats.capacity, config.pools[4].pool.blockCount);
            EXPECT_EQ(payload64Stats.inUse, 0u);
            EXPECT_EQ(payload256Stats.capacity, config.pools[5].pool.blockCount);
            EXPECT_EQ(payload256Stats.inUse, 0u);
            EXPECT_EQ(payload1024Stats.capacity, config.pools[6].pool.blockCount);
            EXPECT_EQ(payload1024Stats.inUse, 0u);
            EXPECT_EQ(payload8192Stats.capacity, config.pools[7].pool.blockCount);
            EXPECT_EQ(payload8192Stats.inUse, 0u);
            EXPECT_EQ(payloadRefControlStats.capacity, config.pools[8].pool.blockCount);
            EXPECT_EQ(payloadRefControlStats.inUse, 0u);
        }

        TEST(NrMemoryPoolManagerTests, CreateWithSingleRoleConfigSucceeds)
        {
            NrMemoryPoolManagerConfig config;
            config.pools = {
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage, MakePoolConfig(1)},
            };

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            ASSERT_TRUE(result.Succeeded());
            ASSERT_TRUE(result.HasValue());

            std::unique_ptr<NrMemoryPoolManager> manager = result.TakeValue();
            ASSERT_NE(manager, nullptr);

            NrResult<NrMemoryPoolStats> queueStatsResult =
                manager->Stats(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            ASSERT_TRUE(queueStatsResult.Succeeded());

            const NrMemoryPoolStats queueStats = queueStatsResult.TakeValue();
            EXPECT_EQ(queueStats.capacity, 1u);
        }

        TEST(NrMemoryPoolManagerTests, PurposeSpecificQueueRolesKeepIndependentStats)
        {
            NrMemoryPoolManagerConfig config;
            config.pools = {
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage, MakePoolConfig(1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::ToWorldEventQueueStorage, MakePoolConfig(2)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::DiagnosticsQueueStorage, MakePoolConfig(3)},
            };

            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult = NrMemoryPoolManager::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> ingressBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            ASSERT_TRUE(ingressBlockResult.Succeeded());
            NrPooledMemoryBlock ingressBlock = ingressBlockResult.TakeValue();
            EXPECT_TRUE(ingressBlock.IsValid());

            NrResult<NrPooledMemoryBlock> diagnosticsBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(diagnosticsBlockResult.Succeeded());
            NrPooledMemoryBlock diagnosticsBlock = diagnosticsBlockResult.TakeValue();
            EXPECT_TRUE(diagnosticsBlock.IsValid());

            NrResult<NrMemoryPoolStats> ingressStatsResult =
                manager->Stats(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            NrResult<NrMemoryPoolStats> toWorldStatsResult =
                manager->Stats(NrMemoryPoolRole::ToWorldEventQueueStorage);
            NrResult<NrMemoryPoolStats> diagnosticsStatsResult =
                manager->Stats(NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(ingressStatsResult.Succeeded());
            ASSERT_TRUE(toWorldStatsResult.Succeeded());
            ASSERT_TRUE(diagnosticsStatsResult.Succeeded());

            const NrMemoryPoolStats ingressStats = ingressStatsResult.TakeValue();
            const NrMemoryPoolStats toWorldStats = toWorldStatsResult.TakeValue();
            const NrMemoryPoolStats diagnosticsStats = diagnosticsStatsResult.TakeValue();

            EXPECT_EQ(ingressStats.capacity, 1u);
            EXPECT_EQ(ingressStats.inUse, 1u);
            EXPECT_EQ(toWorldStats.capacity, 2u);
            EXPECT_EQ(toWorldStats.inUse, 0u);
            EXPECT_EQ(diagnosticsStats.capacity, 3u);
            EXPECT_EQ(diagnosticsStats.inUse, 1u);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFailsWhenKnownRoleIsNotConfigured)
        {
            NrMemoryPoolManagerConfig config;
            config.pools = {
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage, MakePoolConfig(1)},
            };

            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult = NrMemoryPoolManager::Create(config);
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = manager->AcquireBlock(NrMemoryPoolRole::RecvBuffer);

            EXPECT_TRUE(acquireResult.Failed());
            EXPECT_FALSE(acquireResult.HasValue());
            EXPECT_EQ(acquireResult.ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrMemoryPoolManagerTests, CreateFailsWhenRecvBufferConfigIsInvalid)
        {
            NrMemoryPoolManagerConfig config = MakeManagerConfig();
            config.pools[0].pool.blockSize = 0;

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, CreateFailsWhenSendBufferConfigIsInvalid)
        {
            NrMemoryPoolManagerConfig config = MakeManagerConfig();
            config.pools[1].pool.blockCount = 0;

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, CreateFailsWhenOverlappedContextConfigIsInvalid)
        {
            NrMemoryPoolManagerConfig config = MakeManagerConfig();
            config.pools[2].pool.alignment = 0;

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, CreateFailsWhenQueueStorageConfigIsInvalid)
        {
            NrMemoryPoolManagerConfig config = MakeManagerConfig();
            config.pools[3].pool.blockSize = 0;

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, CreateFailsWhenRoleIsDuplicated)
        {
            NrMemoryPoolManagerConfig config = MakeManagerConfig();
            config.pools[1].role = NrMemoryPoolRole::RecvBuffer;

            NrResult<std::unique_ptr<NrMemoryPoolManager>> result = NrMemoryPoolManager::Create(config);

            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFromRecvBufferUsesRecvPool)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = manager->AcquireBlock(NrMemoryPoolRole::RecvBuffer);

            ASSERT_TRUE(acquireResult.Succeeded());
            NrPooledMemoryBlock block = acquireResult.TakeValue();
            EXPECT_TRUE(block.IsValid());

            const NrMemoryPoolManagerStats stats = manager->Stats();
            const NrMemoryPoolStats& recvStats = FindStats(stats, NrMemoryPoolRole::RecvBuffer);
            const NrMemoryPoolStats& sendStats = FindStats(stats, NrMemoryPoolRole::SendBuffer);
            const NrMemoryPoolStats& contextStats = FindStats(stats, NrMemoryPoolRole::OverlappedContext);

            EXPECT_EQ(recvStats.inUse, 1u);
            EXPECT_EQ(recvStats.available, 0u);
            EXPECT_EQ(sendStats.inUse, 0u);
            EXPECT_EQ(sendStats.available, 2u);
            EXPECT_EQ(contextStats.inUse, 0u);
            EXPECT_EQ(contextStats.available, 3u);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFromSendBufferUsesSendPool)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = manager->AcquireBlock(NrMemoryPoolRole::SendBuffer);

            ASSERT_TRUE(acquireResult.Succeeded());
            NrPooledMemoryBlock block = acquireResult.TakeValue();
            EXPECT_TRUE(block.IsValid());

            const NrMemoryPoolManagerStats stats = manager->Stats();
            const NrMemoryPoolStats& recvStats = FindStats(stats, NrMemoryPoolRole::RecvBuffer);
            const NrMemoryPoolStats& sendStats = FindStats(stats, NrMemoryPoolRole::SendBuffer);
            const NrMemoryPoolStats& contextStats = FindStats(stats, NrMemoryPoolRole::OverlappedContext);

            EXPECT_EQ(recvStats.inUse, 0u);
            EXPECT_EQ(recvStats.available, 1u);
            EXPECT_EQ(sendStats.inUse, 1u);
            EXPECT_EQ(sendStats.available, 1u);
            EXPECT_EQ(contextStats.inUse, 0u);
            EXPECT_EQ(contextStats.available, 3u);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFromOverlappedContextUsesOverlappedContextRole)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = manager->AcquireBlock(NrMemoryPoolRole::OverlappedContext);

            ASSERT_TRUE(acquireResult.Succeeded());
            NrPooledMemoryBlock block = acquireResult.TakeValue();
            EXPECT_TRUE(block.IsValid());

            const NrMemoryPoolManagerStats stats = manager->Stats();
            const NrMemoryPoolStats& recvStats = FindStats(stats, NrMemoryPoolRole::RecvBuffer);
            const NrMemoryPoolStats& sendStats = FindStats(stats, NrMemoryPoolRole::SendBuffer);
            const NrMemoryPoolStats& contextStats = FindStats(stats, NrMemoryPoolRole::OverlappedContext);

            EXPECT_EQ(recvStats.inUse, 0u);
            EXPECT_EQ(recvStats.available, 1u);
            EXPECT_EQ(sendStats.inUse, 0u);
            EXPECT_EQ(sendStats.available, 2u);
            EXPECT_EQ(contextStats.inUse, 1u);
            EXPECT_EQ(contextStats.available, 2u);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFromQueueStorageUsesQueueStorageRole)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult =
                manager->AcquireBlock(NrMemoryPoolRole::RuntimeIngressQueueStorage);

            ASSERT_TRUE(acquireResult.Succeeded());
            NrPooledMemoryBlock block = acquireResult.TakeValue();
            EXPECT_TRUE(block.IsValid());

            const NrMemoryPoolManagerStats stats = manager->Stats();
            const NrMemoryPoolStats& recvStats = FindStats(stats, NrMemoryPoolRole::RecvBuffer);
            const NrMemoryPoolStats& sendStats = FindStats(stats, NrMemoryPoolRole::SendBuffer);
            const NrMemoryPoolStats& contextStats = FindStats(stats, NrMemoryPoolRole::OverlappedContext);
            const NrMemoryPoolStats& queueStats = FindStats(stats, NrMemoryPoolRole::RuntimeIngressQueueStorage);

            EXPECT_EQ(recvStats.inUse, 0u);
            EXPECT_EQ(recvStats.available, 1u);
            EXPECT_EQ(sendStats.inUse, 0u);
            EXPECT_EQ(sendStats.available, 2u);
            EXPECT_EQ(contextStats.inUse, 0u);
            EXPECT_EQ(contextStats.available, 3u);
            EXPECT_EQ(queueStats.inUse, 1u);
            EXPECT_EQ(queueStats.available, 3u);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFailsWhenRoleIsUnknown)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = manager->AcquireBlock(static_cast<NrMemoryPoolRole>(999));

            EXPECT_TRUE(acquireResult.Failed());
            EXPECT_FALSE(acquireResult.HasValue());
            EXPECT_EQ(acquireResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, AcquireBlockFailsWhenCountIsUsed)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> acquireResult = manager->AcquireBlock(NrMemoryPoolRole::Count);

            EXPECT_TRUE(acquireResult.Failed());
            EXPECT_FALSE(acquireResult.HasValue());
            EXPECT_EQ(acquireResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrMemoryPoolManagerTests, StatsSnapshotReportsAllPools)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrPooledMemoryBlock> recvBlockResult = manager->AcquireBlock(NrMemoryPoolRole::RecvBuffer);
            NrResult<NrPooledMemoryBlock> firstSendBlockResult = manager->AcquireBlock(NrMemoryPoolRole::SendBuffer);
            NrResult<NrPooledMemoryBlock> secondSendBlockResult = manager->AcquireBlock(NrMemoryPoolRole::SendBuffer);
            NrResult<NrPooledMemoryBlock> contextBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::OverlappedContext);
            NrResult<NrPooledMemoryBlock> queueBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            NrResult<NrPooledMemoryBlock> payload64BlockResult = manager->AcquireBlock(NrMemoryPoolRole::Payload64);
            NrResult<NrPooledMemoryBlock> payload256BlockResult = manager->AcquireBlock(NrMemoryPoolRole::Payload256);
            NrResult<NrPooledMemoryBlock> payload1024BlockResult = manager->AcquireBlock(NrMemoryPoolRole::Payload1024);
            NrResult<NrPooledMemoryBlock> payload8192BlockResult = manager->AcquireBlock(NrMemoryPoolRole::Payload8192);
            NrResult<NrPooledMemoryBlock> payloadRefControlBlockResult =
                manager->AcquireBlock(NrMemoryPoolRole::PayloadRefControl);

            ASSERT_TRUE(recvBlockResult.Succeeded());
            ASSERT_TRUE(firstSendBlockResult.Succeeded());
            ASSERT_TRUE(secondSendBlockResult.Succeeded());
            ASSERT_TRUE(contextBlockResult.Succeeded());
            ASSERT_TRUE(queueBlockResult.Succeeded());
            ASSERT_TRUE(payload64BlockResult.Succeeded());
            ASSERT_TRUE(payload256BlockResult.Succeeded());
            ASSERT_TRUE(payload1024BlockResult.Succeeded());
            ASSERT_TRUE(payload8192BlockResult.Succeeded());
            ASSERT_TRUE(payloadRefControlBlockResult.Succeeded());

            NrPooledMemoryBlock recvBlock = recvBlockResult.TakeValue();
            NrPooledMemoryBlock firstSendBlock = firstSendBlockResult.TakeValue();
            NrPooledMemoryBlock secondSendBlock = secondSendBlockResult.TakeValue();
            NrPooledMemoryBlock contextBlock = contextBlockResult.TakeValue();
            NrPooledMemoryBlock queueBlock = queueBlockResult.TakeValue();
            NrPooledMemoryBlock payload64Block = payload64BlockResult.TakeValue();
            NrPooledMemoryBlock payload256Block = payload256BlockResult.TakeValue();
            NrPooledMemoryBlock payload1024Block = payload1024BlockResult.TakeValue();
            NrPooledMemoryBlock payload8192Block = payload8192BlockResult.TakeValue();
            NrPooledMemoryBlock payloadRefControlBlock = payloadRefControlBlockResult.TakeValue();

            const NrMemoryPoolManagerStats stats = manager->Stats();
            const NrMemoryPoolStats& recvStats = FindStats(stats, NrMemoryPoolRole::RecvBuffer);
            const NrMemoryPoolStats& sendStats = FindStats(stats, NrMemoryPoolRole::SendBuffer);
            const NrMemoryPoolStats& contextStats = FindStats(stats, NrMemoryPoolRole::OverlappedContext);
            const NrMemoryPoolStats& queueStats = FindStats(stats, NrMemoryPoolRole::RuntimeIngressQueueStorage);
            const NrMemoryPoolStats& payload64Stats = FindStats(stats, NrMemoryPoolRole::Payload64);
            const NrMemoryPoolStats& payload256Stats = FindStats(stats, NrMemoryPoolRole::Payload256);
            const NrMemoryPoolStats& payload1024Stats = FindStats(stats, NrMemoryPoolRole::Payload1024);
            const NrMemoryPoolStats& payload8192Stats = FindStats(stats, NrMemoryPoolRole::Payload8192);
            const NrMemoryPoolStats& payloadRefControlStats = FindStats(stats, NrMemoryPoolRole::PayloadRefControl);

            EXPECT_EQ(recvStats.capacity, 1u);
            EXPECT_EQ(recvStats.inUse, 1u);
            EXPECT_EQ(recvStats.available, 0u);
            EXPECT_EQ(sendStats.capacity, 2u);
            EXPECT_EQ(sendStats.inUse, 2u);
            EXPECT_EQ(sendStats.available, 0u);
            EXPECT_EQ(contextStats.capacity, 3u);
            EXPECT_EQ(contextStats.inUse, 1u);
            EXPECT_EQ(contextStats.available, 2u);
            EXPECT_EQ(queueStats.capacity, 4u);
            EXPECT_EQ(queueStats.inUse, 1u);
            EXPECT_EQ(queueStats.available, 3u);
            EXPECT_EQ(payload64Stats.capacity, 5u);
            EXPECT_EQ(payload64Stats.inUse, 1u);
            EXPECT_EQ(payload64Stats.available, 4u);
            EXPECT_EQ(payload256Stats.capacity, 6u);
            EXPECT_EQ(payload256Stats.inUse, 1u);
            EXPECT_EQ(payload256Stats.available, 5u);
            EXPECT_EQ(payload1024Stats.capacity, 7u);
            EXPECT_EQ(payload1024Stats.inUse, 1u);
            EXPECT_EQ(payload1024Stats.available, 6u);
            EXPECT_EQ(payload8192Stats.capacity, 8u);
            EXPECT_EQ(payload8192Stats.inUse, 1u);
            EXPECT_EQ(payload8192Stats.available, 7u);
            EXPECT_EQ(payloadRefControlStats.capacity, 9u);
            EXPECT_EQ(payloadRefControlStats.inUse, 1u);
            EXPECT_EQ(payloadRefControlStats.available, 8u);
        }

        TEST(NrMemoryPoolManagerTests, StatsByRoleReportsSelectedPool)
        {
            NrResult<std::unique_ptr<NrMemoryPoolManager>> createResult =
                NrMemoryPoolManager::Create(MakeManagerConfig());
            ASSERT_TRUE(createResult.Succeeded());
            std::unique_ptr<NrMemoryPoolManager> manager = createResult.TakeValue();

            NrResult<NrMemoryPoolStats> recvStatsResult = manager->Stats(NrMemoryPoolRole::RecvBuffer);
            NrResult<NrMemoryPoolStats> sendStatsResult = manager->Stats(NrMemoryPoolRole::SendBuffer);
            NrResult<NrMemoryPoolStats> contextStatsResult = manager->Stats(NrMemoryPoolRole::OverlappedContext);
            NrResult<NrMemoryPoolStats> queueStatsResult =
                manager->Stats(NrMemoryPoolRole::RuntimeIngressQueueStorage);
            NrResult<NrMemoryPoolStats> payload64StatsResult = manager->Stats(NrMemoryPoolRole::Payload64);
            NrResult<NrMemoryPoolStats> payload256StatsResult = manager->Stats(NrMemoryPoolRole::Payload256);
            NrResult<NrMemoryPoolStats> payload1024StatsResult = manager->Stats(NrMemoryPoolRole::Payload1024);
            NrResult<NrMemoryPoolStats> payload8192StatsResult = manager->Stats(NrMemoryPoolRole::Payload8192);
            NrResult<NrMemoryPoolStats> payloadRefControlStatsResult =
                manager->Stats(NrMemoryPoolRole::PayloadRefControl);

            ASSERT_TRUE(recvStatsResult.Succeeded());
            ASSERT_TRUE(sendStatsResult.Succeeded());
            ASSERT_TRUE(contextStatsResult.Succeeded());
            ASSERT_TRUE(queueStatsResult.Succeeded());
            ASSERT_TRUE(payload64StatsResult.Succeeded());
            ASSERT_TRUE(payload256StatsResult.Succeeded());
            ASSERT_TRUE(payload1024StatsResult.Succeeded());
            ASSERT_TRUE(payload8192StatsResult.Succeeded());
            ASSERT_TRUE(payloadRefControlStatsResult.Succeeded());

            const NrMemoryPoolStats recvStats = recvStatsResult.TakeValue();
            const NrMemoryPoolStats sendStats = sendStatsResult.TakeValue();
            const NrMemoryPoolStats contextStats = contextStatsResult.TakeValue();
            const NrMemoryPoolStats queueStats = queueStatsResult.TakeValue();
            const NrMemoryPoolStats payload64Stats = payload64StatsResult.TakeValue();
            const NrMemoryPoolStats payload256Stats = payload256StatsResult.TakeValue();
            const NrMemoryPoolStats payload1024Stats = payload1024StatsResult.TakeValue();
            const NrMemoryPoolStats payload8192Stats = payload8192StatsResult.TakeValue();
            const NrMemoryPoolStats payloadRefControlStats = payloadRefControlStatsResult.TakeValue();

            EXPECT_EQ(recvStats.capacity, 1u);
            EXPECT_EQ(sendStats.capacity, 2u);
            EXPECT_EQ(contextStats.capacity, 3u);
            EXPECT_EQ(queueStats.capacity, 4u);
            EXPECT_EQ(payload64Stats.capacity, 5u);
            EXPECT_EQ(payload256Stats.capacity, 6u);
            EXPECT_EQ(payload1024Stats.capacity, 7u);
            EXPECT_EQ(payload8192Stats.capacity, 8u);
            EXPECT_EQ(payloadRefControlStats.capacity, 9u);
        }
    } // namespace
} // namespace psnr::core
