#include "pch.h"

#include "NrInputFactory.h"
#include "NrMemoryPoolTestUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};
        constexpr NrPacketType MoveInputPacketType{1};

        [[nodiscard]] std::vector<std::byte> MakePacketBytes(NrPacketType packetType,
                                                             std::span<const std::byte> payloadBytes = {})
        {
            const std::uint16_t packetLength = static_cast<std::uint16_t>(NrPacketHeaderLength + payloadBytes.size());
            std::vector<std::byte> packetBytes;
            packetBytes.reserve(packetLength);
            packetBytes.push_back(static_cast<std::byte>(packetLength & 0xFF));
            packetBytes.push_back(static_cast<std::byte>((packetLength >> 8) & 0xFF));

            const std::uint16_t rawPacketType = packetType.value;
            packetBytes.push_back(static_cast<std::byte>(rawPacketType & 0xFF));
            packetBytes.push_back(static_cast<std::byte>((rawPacketType >> 8) & 0xFF));

            packetBytes.push_back(std::byte{1}); // version
            packetBytes.push_back(std::byte{0}); // flags
            packetBytes.insert(packetBytes.end(), payloadBytes.begin(), payloadBytes.end());
            return packetBytes;
        }

        [[nodiscard]] NrPacketParseResult MakeCompleteParseResult(std::span<const std::byte> packetBytes,
                                                                  NrPacketType packetType)
        {
            NrPacketParseResult parseResult;
            parseResult.status = NrPacketParseStatus::Complete;
            parseResult.header.packetLength = static_cast<std::uint16_t>(packetBytes.size());
            parseResult.header.packetType = packetType.value;
            parseResult.header.version = 1;
            parseResult.header.flags = 0;
            parseResult.packetBytes = packetBytes;
            return parseResult;
        }

        TEST(NrInputFactoryTests, CreateInputFromZeroPayloadDoesNotAcquirePayloadBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            NrInputFactory factory(*manager);
            const std::vector<std::byte> packetBytes = MakePacketBytes(HeartbeatPacketType);
            const NrPacketParseResult parseResult =
                MakeCompleteParseResult(std::span(packetBytes), HeartbeatPacketType);
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            NrResult<NrInput> inputResult = factory.CreateInput(42, parseResult, rule);

            ASSERT_TRUE(inputResult.Succeeded());
            NrInput input = inputResult.TakeValue();
            EXPECT_EQ(input.sessionId, 42u);
            EXPECT_EQ(input.packetType, HeartbeatPacketType);
            EXPECT_EQ(input.dispatchLane, NrDispatchLane::ServerIngress);
            EXPECT_TRUE(input.payload.IsEmpty());
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 0u);
        }

        TEST(NrInputFactoryTests, CreateInputCopiesPayloadBytesIntoOwnedInput)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            NrInputFactory factory(*manager);
            const std::array<std::byte, 3> payloadBytes = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
            std::vector<std::byte> packetBytes = MakePacketBytes(HeartbeatPacketType, std::span(payloadBytes));
            const NrPacketParseResult parseResult =
                MakeCompleteParseResult(std::span(packetBytes), HeartbeatPacketType);
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            NrResult<NrInput> inputResult = factory.CreateInput(7, parseResult, rule);

            ASSERT_TRUE(inputResult.Succeeded());
            NrInput input = inputResult.TakeValue();
            packetBytes[NrPacketHeaderLength] = std::byte{0xFF};

            EXPECT_EQ(input.payload.Length(), payloadBytes.size());
            EXPECT_TRUE(std::ranges::equal(input.payload.Bytes(), payloadBytes));
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 1u);
        }

        TEST(NrInputFactoryTests, CreateInputRejectsNonCompleteParseResult)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            NrInputFactory factory(*manager);
            NrPacketParseResult parseResult;
            parseResult.status = NrPacketParseStatus::NeedMoreData;
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            NrResult<NrInput> inputResult = factory.CreateInput(1, parseResult, rule);

            EXPECT_TRUE(inputResult.Failed());
            EXPECT_EQ(inputResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrInputFactoryTests, CreateInputRejectsPacketTypeMismatch)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            NrInputFactory factory(*manager);
            const std::vector<std::byte> packetBytes = MakePacketBytes(MoveInputPacketType);
            const NrPacketParseResult parseResult =
                MakeCompleteParseResult(std::span(packetBytes), MoveInputPacketType);
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            NrResult<NrInput> inputResult = factory.CreateInput(1, parseResult, rule);

            EXPECT_TRUE(inputResult.Failed());
            EXPECT_EQ(inputResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrInputFactoryTests, CreateInputRejectsPacketBytesShorterThanHeader)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            NrInputFactory factory(*manager);
            const std::array<std::byte, NrPacketHeaderLength - 1> packetBytes = {};
            const NrPacketParseResult parseResult =
                MakeCompleteParseResult(std::span(packetBytes), HeartbeatPacketType);
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            NrResult<NrInput> inputResult = factory.CreateInput(1, parseResult, rule);

            EXPECT_TRUE(inputResult.Failed());
            EXPECT_EQ(inputResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrInputFactoryTests, CreateInputPropagatesPayloadAllocationFailure)
        {
            NrMemoryPoolManagerConfig config =
                test::MakeMemoryPoolManagerConfigWith(NrMemoryPoolRole::Payload64, test::MakePoolConfig(64, 1));
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager(config);
            ASSERT_NE(manager, nullptr);
            NrResult<NrPooledMemoryBlock> reservedBlockResult = manager->AcquireBlock(NrMemoryPoolRole::Payload64);
            ASSERT_TRUE(reservedBlockResult.Succeeded());
            NrPooledMemoryBlock reservedBlock = reservedBlockResult.TakeValue();
            NrInputFactory factory(*manager);
            const std::array<std::byte, 1> payloadBytes = {std::byte{0x44}};
            const std::vector<std::byte> packetBytes =
                MakePacketBytes(HeartbeatPacketType, std::span(payloadBytes));
            const NrPacketParseResult parseResult =
                MakeCompleteParseResult(std::span(packetBytes), HeartbeatPacketType);
            const NrPacketDispatchRule rule{HeartbeatPacketType, NrDispatchLane::ServerIngress};

            NrResult<NrInput> inputResult = factory.CreateInput(1, parseResult, rule);

            EXPECT_TRUE(inputResult.Failed());
            EXPECT_EQ(inputResult.ErrorCode(), NrErrorCode::PoolExhausted);
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload64).inUse, 1u);
        }
    } // namespace
} // namespace psnr::core
