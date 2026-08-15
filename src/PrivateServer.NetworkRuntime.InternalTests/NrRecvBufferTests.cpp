#include "pch.h"

#include "NrRecvBuffer.h"

#include "NrMemoryPoolManager.h"

#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

namespace psnr::core
{
    namespace
    {
        [[nodiscard]] NrMemoryPoolConfig MakePoolConfig(std::size_t blockSize, std::size_t blockCount,
                                                        std::size_t alignment = 16)
        {
            NrMemoryPoolConfig config;
            config.blockSize = blockSize;
            config.blockCount = blockCount;
            config.alignment = alignment;
            return config;
        }

        [[nodiscard]] NrMemoryPoolManagerConfig MakeManagerConfig(std::size_t recvBlockSize = 16,
                                                                  std::size_t recvBlockCount = 1)
        {
            NrMemoryPoolManagerConfig config;
            config.pools = {
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RecvBuffer,
                                              MakePoolConfig(recvBlockSize, recvBlockCount)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::SendBuffer, MakePoolConfig(16, 1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::OverlappedContext, MakePoolConfig(64, 1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage,
                                              MakePoolConfig(64, 1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload64, MakePoolConfig(64, 1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload256, MakePoolConfig(256, 1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload1024, MakePoolConfig(1024, 1)},
                NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::Payload8192, MakePoolConfig(8192, 1)},
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

        [[nodiscard]] NrMemoryPoolStats RecvBufferStats(NrMemoryPoolManager& manager)
        {
            NrResult<NrMemoryPoolStats> statsResult = manager.Stats(NrMemoryPoolRole::RecvBuffer);
            EXPECT_TRUE(statsResult.Succeeded());
            if (statsResult.Failed())
            {
                return NrMemoryPoolStats{};
            }

            return statsResult.TakeValue();
        }

        TEST(NrRecvBufferTests, RecvBufferIsMoveOnly)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrRecvBuffer>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrRecvBuffer>);
            EXPECT_TRUE(std::is_move_constructible_v<NrRecvBuffer>);
            EXPECT_TRUE(std::is_move_assignable_v<NrRecvBuffer>);
        }

        TEST(NrRecvBufferTests, CreateWithValidCapacityAcquiresRecvBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 12);

            ASSERT_TRUE(createResult.Succeeded());
            ASSERT_TRUE(createResult.HasValue());

            NrRecvBuffer buffer = createResult.TakeValue();
            EXPECT_EQ(buffer.Capacity(), 12u);
            EXPECT_EQ(buffer.ReadableBytes(), 0u);
            EXPECT_EQ(buffer.WritableBytes(), 12u);
            EXPECT_EQ(buffer.ConsumedBytes(), 0u);

            const NrMemoryPoolStats stats = RecvBufferStats(*manager);
            EXPECT_EQ(stats.inUse, 1u);
            EXPECT_EQ(stats.available, 0u);
        }

        TEST(NrRecvBufferTests, CreateFailsWhenCapacityIsZero)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 0);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(RecvBufferStats(*manager).inUse, 0u);
        }

        TEST(NrRecvBufferTests, CreateFailsWhenRecvPoolIsExhausted)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            NrResult<NrPooledMemoryBlock> reservedBlockResult = manager->AcquireBlock(NrMemoryPoolRole::RecvBuffer);
            ASSERT_TRUE(reservedBlockResult.Succeeded());
            NrPooledMemoryBlock reservedBlock = reservedBlockResult.TakeValue();

            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 8);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::PoolExhausted);
        }

        TEST(NrRecvBufferTests, CreateFailsWhenRecvBlockIsTooSmall)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager(MakeManagerConfig(8));
            ASSERT_NE(manager, nullptr);

            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 12);

            EXPECT_TRUE(createResult.Failed());
            EXPECT_FALSE(createResult.HasValue());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::CapacityExceeded);
            EXPECT_EQ(RecvBufferStats(*manager).inUse, 0u);
        }

        TEST(NrRecvBufferTests, DestroyReturnsRecvBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);

            {
                NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 16);
                ASSERT_TRUE(createResult.Succeeded());
                NrRecvBuffer buffer = createResult.TakeValue();

                EXPECT_EQ(RecvBufferStats(*manager).inUse, 1u);
            }

            const NrMemoryPoolStats stats = RecvBufferStats(*manager);
            EXPECT_EQ(stats.inUse, 0u);
            EXPECT_EQ(stats.available, 1u);
        }

        TEST(NrRecvBufferTests, CommitWrittenAdvancesWriteCursor)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 16);
            ASSERT_TRUE(createResult.Succeeded());
            NrRecvBuffer buffer = createResult.TakeValue();

            std::span<std::byte> initialWritable = buffer.WritableSpan();
            ASSERT_EQ(initialWritable.size(), 16u);

            EXPECT_TRUE(buffer.CommitWritten(5).Succeeded());

            EXPECT_EQ(buffer.ReadableBytes(), 5u);
            EXPECT_EQ(buffer.WritableBytes(), 11u);
            EXPECT_EQ(buffer.ReadableSpan().data(), initialWritable.data());
            EXPECT_EQ(buffer.WritableSpan().data(), initialWritable.data() + 5);
        }

        TEST(NrRecvBufferTests, CommitWrittenRejectsBytesBeyondWritableTailAndKeepsCursors)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 8);
            ASSERT_TRUE(createResult.Succeeded());
            NrRecvBuffer buffer = createResult.TakeValue();

            EXPECT_TRUE(buffer.CommitWritten(3).Succeeded());

            const NrStatus status = buffer.CommitWritten(6);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(buffer.ReadableBytes(), 3u);
            EXPECT_EQ(buffer.WritableBytes(), 5u);
            EXPECT_EQ(buffer.ConsumedBytes(), 0u);
        }

        TEST(NrRecvBufferTests, ConsumeAdvancesReadCursor)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 8);
            ASSERT_TRUE(createResult.Succeeded());
            NrRecvBuffer buffer = createResult.TakeValue();

            ASSERT_TRUE(buffer.CommitWritten(6).Succeeded());
            const std::byte* const initialReadable = buffer.ReadableSpan().data();

            EXPECT_TRUE(buffer.Consume(2).Succeeded());

            EXPECT_EQ(buffer.ConsumedBytes(), 2u);
            EXPECT_EQ(buffer.ReadableBytes(), 4u);
            EXPECT_EQ(buffer.WritableBytes(), 2u);
            EXPECT_EQ(buffer.ReadableSpan().data(), initialReadable + 2);
        }

        TEST(NrRecvBufferTests, ConsumeRejectsBytesBeyondReadableRangeAndKeepsCursors)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 8);
            ASSERT_TRUE(createResult.Succeeded());
            NrRecvBuffer buffer = createResult.TakeValue();

            ASSERT_TRUE(buffer.CommitWritten(4).Succeeded());
            ASSERT_TRUE(buffer.Consume(1).Succeeded());

            const NrStatus status = buffer.Consume(4);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(buffer.ConsumedBytes(), 1u);
            EXPECT_EQ(buffer.ReadableBytes(), 3u);
            EXPECT_EQ(buffer.WritableBytes(), 4u);
        }

        TEST(NrRecvBufferTests, CompactMovesReadableBytesToFront)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager();
            ASSERT_NE(manager, nullptr);
            NrResult<NrRecvBuffer> createResult = NrRecvBuffer::Create(*manager, 8);
            ASSERT_TRUE(createResult.Succeeded());
            NrRecvBuffer buffer = createResult.TakeValue();

            std::span<std::byte> writable = buffer.WritableSpan();
            for (std::size_t index = 0; index < 6; ++index)
            {
                writable[index] = static_cast<std::byte>('A' + index);
            }

            ASSERT_TRUE(buffer.CommitWritten(6).Succeeded());
            ASSERT_TRUE(buffer.Consume(2).Succeeded());

            buffer.Compact();

            const std::span<const std::byte> readable = buffer.ReadableSpan();
            ASSERT_EQ(readable.size(), 4u);
            EXPECT_EQ(std::to_integer<int>(readable[0]), 'C');
            EXPECT_EQ(std::to_integer<int>(readable[1]), 'D');
            EXPECT_EQ(std::to_integer<int>(readable[2]), 'E');
            EXPECT_EQ(std::to_integer<int>(readable[3]), 'F');
            EXPECT_EQ(buffer.ConsumedBytes(), 0u);
            EXPECT_EQ(buffer.ReadableBytes(), 4u);
            EXPECT_EQ(buffer.WritableBytes(), 4u);
            EXPECT_EQ(buffer.WritableSpan().data(), writable.data() + 4);
        }

        TEST(NrRecvBufferTests, MoveTransfersPoolBlockOwnership)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateManager(MakeManagerConfig(16, 2));
            ASSERT_NE(manager, nullptr);

            {
                NrResult<NrRecvBuffer> firstResult = NrRecvBuffer::Create(*manager, 16);
                ASSERT_TRUE(firstResult.Succeeded());
                NrRecvBuffer first = firstResult.TakeValue();
                ASSERT_TRUE(first.CommitWritten(3).Succeeded());

                NrResult<NrRecvBuffer> secondResult = NrRecvBuffer::Create(*manager, 16);
                ASSERT_TRUE(secondResult.Succeeded());
                NrRecvBuffer second = secondResult.TakeValue();

                second = std::move(first);

                EXPECT_EQ(first.Capacity(), 0u);
                EXPECT_EQ(first.ReadableBytes(), 0u);
                EXPECT_EQ(first.WritableBytes(), 0u);
                EXPECT_EQ(second.Capacity(), 16u);
                EXPECT_EQ(second.ReadableBytes(), 3u);
                EXPECT_EQ(RecvBufferStats(*manager).inUse, 1u);
            }

            EXPECT_EQ(RecvBufferStats(*manager).inUse, 0u);
        }
    } // namespace
} // namespace psnr::core
