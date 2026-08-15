#include "pch.h"

#include "NrMemoryPoolTestUtils.h"
#include "NrPayload.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace psnr::core
{
    namespace
    {
        TEST(NrPayloadTests, PayloadIsMoveOnly)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrPayload>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrPayload>);
            EXPECT_TRUE(std::is_move_constructible_v<NrPayload>);
            EXPECT_TRUE(std::is_move_assignable_v<NrPayload>);
        }

        TEST(NrPayloadTests, CreatePayloadFromEmptyPayloadDoesNotAcquireBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            const std::span<const std::byte> bytes;
            NrResult<NrPayload> createResult = NrPayloadFactory::CreatePayloadFrom(*manager, bytes);

            ASSERT_TRUE(createResult.Succeeded());
            NrPayload payload = createResult.TakeValue();

            EXPECT_TRUE(payload.IsEmpty());
            EXPECT_EQ(payload.Length(), 0u);
            EXPECT_EQ(payload.Capacity(), 0u);
            EXPECT_TRUE(payload.Bytes().empty());
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 0u);
        }

        TEST(NrPayloadTests, CreatePayloadFromCopiesBytesIntoPayload64Block)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 3> bytes = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

            {
                NrResult<NrPayload> createResult = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(bytes));

                ASSERT_TRUE(createResult.Succeeded());
                NrPayload payload = createResult.TakeValue();

                EXPECT_FALSE(payload.IsEmpty());
                EXPECT_EQ(payload.Length(), bytes.size());
                EXPECT_EQ(payload.Capacity(), 64u);
                EXPECT_TRUE(std::ranges::equal(payload.Bytes(), bytes));
                EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 1u);
            }

            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 0u);
        }

        TEST(NrPayloadTests, CreatePayloadFromSelectsPayloadSizeClass)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            const std::vector<std::byte> payload256(65, std::byte{0x11});
            const std::vector<std::byte> payload1024(257, std::byte{0x22});
            const std::vector<std::byte> payload8192(1025, std::byte{0x33});

            NrResult<NrPayload> result256 = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(payload256));
            NrResult<NrPayload> result1024 = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(payload1024));
            NrResult<NrPayload> result8192 = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(payload8192));

            ASSERT_TRUE(result256.Succeeded());
            ASSERT_TRUE(result1024.Succeeded());
            ASSERT_TRUE(result8192.Succeeded());

            NrPayload payload256Block = result256.TakeValue();
            NrPayload payload1024Block = result1024.TakeValue();
            NrPayload payload8192Block = result8192.TakeValue();

            EXPECT_EQ(payload256Block.Capacity(), 256u);
            EXPECT_EQ(payload1024Block.Capacity(), 1024u);
            EXPECT_EQ(payload8192Block.Capacity(), 8192u);
        }

        TEST(NrPayloadTests, CreatePayloadFromRejectsPayloadLargerThanMaxSizeClass)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::vector<std::byte> bytes(8193, std::byte{0x44});

            NrResult<NrPayload> createResult = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(bytes));

            EXPECT_TRUE(createResult.Failed());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::CapacityExceeded);
        }

        TEST(NrPayloadTests, CreatePayloadFromPropagatesPoolAcquireFailure)
        {
            NrMemoryPoolManagerConfig config =
                test::MakeMemoryPoolManagerConfigWith(NrMemoryPoolRole::Payload64, test::MakePoolConfig(64, 1));
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager(config);
            ASSERT_NE(manager, nullptr);
            NrResult<NrPooledMemoryBlock> reservedBlockResult = manager->AcquireBlock(NrMemoryPoolRole::Payload64);
            ASSERT_TRUE(reservedBlockResult.Succeeded());
            NrPooledMemoryBlock reservedBlock = reservedBlockResult.TakeValue();
            const std::array<std::byte, 1> bytes = {std::byte{0x55}};

            NrResult<NrPayload> createResult = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(bytes));

            EXPECT_TRUE(createResult.Failed());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::PoolExhausted);
        }

        TEST(NrPayloadTests, CreatePayloadFromReturnsBlockWhenConfiguredPayloadPoolIsTooSmall)
        {
            NrMemoryPoolManagerConfig config =
                test::MakeMemoryPoolManagerConfigWith(NrMemoryPoolRole::Payload64, test::MakePoolConfig(8, 1));
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager(config);
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 9> bytes = {};

            NrResult<NrPayload> createResult = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(bytes));

            EXPECT_TRUE(createResult.Failed());
            EXPECT_EQ(createResult.ErrorCode(), NrErrorCode::CapacityExceeded);
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 0u);
        }
    } // namespace
} // namespace psnr::core
