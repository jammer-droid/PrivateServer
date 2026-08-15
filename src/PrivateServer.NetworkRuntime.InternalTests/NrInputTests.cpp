#include "pch.h"

#include "NrInput.h"
#include "NrMemoryPoolTestUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};

        TEST(NrInputTests, InputIsMoveOnly)
        {
            EXPECT_FALSE(std::is_copy_constructible_v<NrInput>);
            EXPECT_FALSE(std::is_copy_assignable_v<NrInput>);
            EXPECT_TRUE(std::is_move_constructible_v<NrInput>);
            EXPECT_TRUE(std::is_move_assignable_v<NrInput>);
        }

        TEST(NrInputTests, ConstructStoresDispatchMetadataAndOwnedPayload)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 3> bytes = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};

            {
                NrResult<NrPayload> payloadResult = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(bytes));
                ASSERT_TRUE(payloadResult.Succeeded());

                NrInput input(42, HeartbeatPacketType, NrDispatchLane::ServerIngress, payloadResult.TakeValue());

                EXPECT_EQ(input.sessionId, 42u);
                EXPECT_EQ(input.packetType, HeartbeatPacketType);
                EXPECT_EQ(input.dispatchLane, NrDispatchLane::ServerIngress);
                EXPECT_EQ(input.payload.Length(), bytes.size());
                EXPECT_TRUE(
                    std::equal(input.payload.Bytes().begin(), input.payload.Bytes().end(), bytes.begin(), bytes.end()));
                EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 1u);
            }

            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 0u);
        }

        TEST(NrInputTests, MoveTransfersPayloadOwnership)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 1> bytes = {std::byte{0x44}};

            {
                NrResult<NrPayload> payloadResult = NrPayloadFactory::CreatePayloadFrom(*manager, std::span(bytes));
                ASSERT_TRUE(payloadResult.Succeeded());
                NrInput input(7, HeartbeatPacketType, NrDispatchLane::ServerIngress, payloadResult.TakeValue());

                NrInput moved(std::move(input));

                EXPECT_EQ(moved.sessionId, 7u);
                EXPECT_EQ(moved.packetType, HeartbeatPacketType);
                EXPECT_EQ(moved.dispatchLane, NrDispatchLane::ServerIngress);
                EXPECT_EQ(moved.payload.Length(), bytes.size());
                EXPECT_TRUE(input.payload.IsEmpty());
                EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 1u);
            }

            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 0u);
        }
    } // namespace
} // namespace psnr::core
