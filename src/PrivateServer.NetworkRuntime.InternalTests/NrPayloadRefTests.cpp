#include "pch.h"

#include "NrMemoryPoolTestUtils.h"
#include "NrPacketHeaderCodec.h"
#include "NrPacketParser.h"
#include "NrPayloadRef.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType TestPacketType{0x1234};

        TEST(NrPayloadRefTests, CreateFramedPayloadRefAllowsEmptySemanticPayload)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            NrResult<NrPayloadRef> result = NrPayloadRefFactory::CreateFramedPayloadRef(*manager, TestPacketType, {});

            ASSERT_TRUE(result.Succeeded());
            NrPayloadRef payload = result.TakeValue();
            const std::array<std::byte, NrPacketHeaderLength> expected = {
                std::byte{0x06}, std::byte{0x00}, std::byte{0x34},
                std::byte{0x12}, static_cast<std::byte>(NrCurrentProtocolVersion), std::byte{0x00},
            };
            EXPECT_EQ(payload.Length(), NrPacketHeaderLength);
            EXPECT_TRUE(std::ranges::equal(payload.Bytes(), expected));
        }

        TEST(NrPayloadRefTests, CreateFramedPayloadRefCopiesSemanticPayloadIntoFinalBlock)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::array<std::byte, 3> semanticPayload = {
                std::byte{0x11},
                std::byte{0x22},
                std::byte{0x33},
            };

            NrResult<NrPayloadRef> result =
                NrPayloadRefFactory::CreateFramedPayloadRef(*manager, TestPacketType, std::span(semanticPayload));
            semanticPayload[0] = std::byte{0xFF};

            ASSERT_TRUE(result.Succeeded());
            NrPayloadRef payload = result.TakeValue();
            ASSERT_EQ(payload.Length(), NrPacketHeaderLength + 3);
            const NrPacketHeader header = NrPacketHeaderCodec::Decode(
                std::span<const std::byte, NrPacketHeaderLength>(payload.Bytes().data(), NrPacketHeaderLength));
            EXPECT_EQ(header.packetLength, NrPacketHeaderLength + 3);
            EXPECT_EQ(header.packetType, TestPacketType.value);
            EXPECT_EQ(header.version, NrCurrentProtocolVersion);
            EXPECT_EQ(header.flags, 0u);
            EXPECT_EQ(payload.Bytes()[NrPacketHeaderLength], std::byte{0x11});
            EXPECT_EQ(payload.Bytes()[NrPacketHeaderLength + 1], std::byte{0x22});
            EXPECT_EQ(payload.Bytes()[NrPacketHeaderLength + 2], std::byte{0x33});
        }

        TEST(NrPayloadRefTests, CreateFramedPayloadRefProducesFrameAcceptedByDefaultParser)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 3> semanticPayload = {
                std::byte{0x11},
                std::byte{0x22},
                std::byte{0x33},
            };
            NrResult<NrPayloadRef> payloadResult =
                NrPayloadRefFactory::CreateFramedPayloadRef(*manager, TestPacketType, std::span(semanticPayload));
            ASSERT_TRUE(payloadResult.Succeeded());
            NrPayloadRef payload = payloadResult.TakeValue();
            NrResult<NrPacketParser> parserResult = NrPacketParser::Create(NrPacketParserConfig{});
            ASSERT_TRUE(parserResult.Succeeded());
            const NrPacketParser parser = parserResult.TakeValue();
            NrPacketParseResult parseResult;

            const NrStatus status = parser.Parse(payload.Bytes(), parseResult);

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(parseResult.status, NrPacketParseStatus::Complete);
            EXPECT_EQ(parseResult.header.packetType, TestPacketType.value);
            EXPECT_EQ(parseResult.header.version, NrCurrentProtocolVersion);
            EXPECT_EQ(parseResult.packetBytes.data(), payload.Bytes().data());
            EXPECT_EQ(parseResult.packetBytes.size(), payload.Length());
        }

        TEST(NrPayloadRefTests, SharedFramedPayloadUsesSameImmutableBytes)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::array<std::byte, 1> semanticPayload = {std::byte{0x44}};
            NrResult<NrPayloadRef> result =
                NrPayloadRefFactory::CreateFramedPayloadRef(*manager, TestPacketType, std::span(semanticPayload));
            ASSERT_TRUE(result.Succeeded());
            NrPayloadRef payload = result.TakeValue();

            NrPayloadRef shared = payload.Share();

            EXPECT_EQ(shared.Bytes().data(), payload.Bytes().data());
            EXPECT_EQ(shared.Length(), payload.Length());
            EXPECT_TRUE(std::ranges::equal(shared.Bytes(), payload.Bytes()));
        }

        TEST(NrPayloadRefTests, CreateFramedPayloadRefAcceptsMaximumWireLength)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::vector<std::byte> semanticPayload(NrMaxPacketLength - NrPacketHeaderLength, std::byte{0x55});

            NrResult<NrPayloadRef> result =
                NrPayloadRefFactory::CreateFramedPayloadRef(*manager, TestPacketType, std::span(semanticPayload));

            ASSERT_TRUE(result.Succeeded());
            EXPECT_EQ(result.Value().Length(), NrMaxPacketLength);
        }

        TEST(NrPayloadRefTests, CreateFramedPayloadRefRejectsWireLengthOverflow)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = test::CreateMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            const std::vector<std::byte> semanticPayload(NrMaxPacketLength - NrPacketHeaderLength + 1,
                                                         std::byte{0x66});

            NrResult<NrPayloadRef> result =
                NrPayloadRefFactory::CreateFramedPayloadRef(*manager, TestPacketType, std::span(semanticPayload));

            EXPECT_TRUE(result.Failed());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::CapacityExceeded);
            EXPECT_EQ(test::Stats(*manager, NrMemoryPoolRole::Payload8192).inUse, 0u);
        }
    } // namespace
} // namespace psnr::core
