#include "pch.h"

#include "NrPacketHeader.h"
#include "NrPacketHeaderCodec.h"
#include "NrPacketParser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::core
{
    namespace
    {
        [[nodiscard]] std::array<std::byte, NrPacketHeaderLength> MakeHeaderBytes(std::uint16_t packetLength,
                                                                                  std::uint16_t packetType = 1,
                                                                                  std::uint8_t version = NrCurrentProtocolVersion,
                                                                                  std::uint8_t flags = 0)
        {
            return {
                static_cast<std::byte>(packetLength & 0xFF),
                static_cast<std::byte>((packetLength >> 8) & 0xFF),
                static_cast<std::byte>(packetType & 0xFF),
                static_cast<std::byte>((packetType >> 8) & 0xFF),
                static_cast<std::byte>(version),
                static_cast<std::byte>(flags),
            };
        }

        [[nodiscard]] NrPacketParser MakeParser(const NrPacketParserConfig& config = NrPacketParserConfig{})
        {
            NrResult<NrPacketParser> parserResult = NrPacketParser::Create(config);
            EXPECT_TRUE(parserResult.Succeeded());
            return parserResult.TakeValue();
        }

        TEST(NrPacketHeaderTests, HeaderLengthMatchesWireContract)
        {
            EXPECT_EQ(NrPacketHeaderLength, 6u);
        }

        TEST(NrPacketHeaderCodecTests, DecodeReadsHeaderFieldsFromProtocolByteOrder)
        {
            const std::array<std::byte, NrPacketHeaderLength> bytes = {
                std::byte{0x10}, std::byte{0x00}, // packetLength = 16
                std::byte{0x34}, std::byte{0x12}, // packetType = 0x1234
                std::byte{0x01},                  // version
                std::byte{0x00},                  // flags
            };

            const NrPacketHeader header =
                NrPacketHeaderCodec::Decode(std::span<const std::byte, NrPacketHeaderLength>(bytes));

            EXPECT_EQ(header.packetLength, 16u);
            EXPECT_EQ(header.packetType, 0x1234u);
            EXPECT_EQ(header.version, 1u);
            EXPECT_EQ(header.flags, 0u);
        }

        TEST(NrPacketParserConfigTests, DefaultsMatchProtocolBaseline)
        {
            const NrPacketParserConfig config;

            EXPECT_EQ(config.maxPacketSize, 8u * 1024u);
            EXPECT_EQ(config.supportedVersion, NrCurrentProtocolVersion);
        }

        TEST(NrPacketParseResultTests, DefaultsRepresentNeedMoreData)
        {
            const NrPacketParseResult output;

            EXPECT_EQ(output.status, NrPacketParseStatus::NeedMoreData);
            EXPECT_EQ(output.protocolErrorReason, NrProtocolErrorReason::None);
            EXPECT_EQ(output.header.packetLength, 0u);
            EXPECT_EQ(output.header.packetType, 0u);
            EXPECT_EQ(output.header.version, 0u);
            EXPECT_EQ(output.header.flags, 0u);
            EXPECT_TRUE(output.packetBytes.empty());
        }

        TEST(NrPacketParseResultTests, StoresCompletePacketViewWithoutPayloadView)
        {
            const std::array<std::byte, NrPacketHeaderLength> bytes = {
                std::byte{0x06}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00},
            };

            NrPacketParseResult output;
            output.status = NrPacketParseStatus::Complete;
            output.header = NrPacketHeaderCodec::Decode(std::span<const std::byte, NrPacketHeaderLength>(bytes));
            output.packetBytes = std::span<const std::byte>(bytes);

            EXPECT_EQ(output.status, NrPacketParseStatus::Complete);
            EXPECT_EQ(output.header.packetLength, NrPacketHeaderLength);
            EXPECT_EQ(output.packetBytes.data(), bytes.data());
            EXPECT_EQ(output.packetBytes.size(), bytes.size());
        }

        TEST(NrPacketParserTests, CreateRejectsMaxPacketSizeSmallerThanHeaderLength)
        {
            NrPacketParserConfig config;
            config.maxPacketSize = NrPacketHeaderLength - 1;

            const NrResult<NrPacketParser> parserResult = NrPacketParser::Create(config);

            EXPECT_TRUE(parserResult.Failed());
            EXPECT_EQ(parserResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrPacketParserTests, CreateRejectsZeroSupportedVersion)
        {
            NrPacketParserConfig config;
            config.supportedVersion = 0;

            const NrResult<NrPacketParser> parserResult = NrPacketParser::Create(config);

            EXPECT_TRUE(parserResult.Failed());
            EXPECT_EQ(parserResult.ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrPacketParserTests, ParseReturnsNeedMoreDataWhenHeaderIsIncomplete)
        {
            const NrPacketParser parser = MakeParser();
            const std::array<std::byte, 2> bytes = {std::byte{0x06}, std::byte{0x00}};
            NrPacketParseResult output;
            output.status = NrPacketParseStatus::Complete;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(output.status, NrPacketParseStatus::NeedMoreData);
            EXPECT_TRUE(output.packetBytes.empty());
        }

        TEST(NrPacketParserTests, ParseRejectsPacketLengthSmallerThanHeaderLength)
        {
            const NrPacketParser parser = MakeParser();
            const std::array<std::byte, NrPacketHeaderLength> bytes = MakeHeaderBytes(5);
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::ProtocolError);
            EXPECT_EQ(output.status, NrPacketParseStatus::ProtocolError);
            EXPECT_EQ(output.protocolErrorReason, NrProtocolErrorReason::InvalidLength);
        }

        TEST(NrPacketParserTests, ParseRejectsPacketLengthGreaterThanMaxPacketSize)
        {
            NrPacketParserConfig config;
            config.maxPacketSize = NrPacketHeaderLength;
            const NrPacketParser parser = MakeParser(config);
            const std::array<std::byte, NrPacketHeaderLength> bytes =
                MakeHeaderBytes(static_cast<std::uint16_t>(NrPacketHeaderLength + 1));
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::ProtocolError);
            EXPECT_EQ(output.status, NrPacketParseStatus::ProtocolError);
            EXPECT_EQ(output.protocolErrorReason, NrProtocolErrorReason::PacketTooLarge);
        }

        TEST(NrPacketParserTests, ParseRejectsUnsupportedVersion)
        {
            const NrPacketParser parser = MakeParser();
            const std::array<std::byte, NrPacketHeaderLength> bytes =
                MakeHeaderBytes(6, 1, static_cast<std::uint8_t>(NrCurrentProtocolVersion + 1));
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::ProtocolError);
            EXPECT_EQ(output.status, NrPacketParseStatus::ProtocolError);
            EXPECT_EQ(output.protocolErrorReason, NrProtocolErrorReason::UnsupportedVersion);
        }

        TEST(NrPacketParserTests, ParseRejectsReservedFlags)
        {
            const NrPacketParser parser = MakeParser();
            const std::array<std::byte, NrPacketHeaderLength> bytes = MakeHeaderBytes(6, 1, 1, 1);
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::ProtocolError);
            EXPECT_EQ(output.status, NrPacketParseStatus::ProtocolError);
            EXPECT_EQ(output.protocolErrorReason, NrProtocolErrorReason::ReservedFlags);
        }

        TEST(NrPacketParserTests, ParseReturnsNeedMoreDataWhenPayloadIsIncomplete)
        {
            const NrPacketParser parser = MakeParser();
            const std::array<std::byte, NrPacketHeaderLength> bytes = MakeHeaderBytes(8);
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(output.status, NrPacketParseStatus::NeedMoreData);
            EXPECT_TRUE(output.packetBytes.empty());
        }

        TEST(NrPacketParserTests, ParseCompletesZeroPayloadPacket)
        {
            const NrPacketParser parser = MakeParser();
            const std::array<std::byte, NrPacketHeaderLength> bytes = MakeHeaderBytes(6, 0x1234);
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(output.status, NrPacketParseStatus::Complete);
            EXPECT_EQ(output.header.packetLength, 6u);
            EXPECT_EQ(output.header.packetType, 0x1234u);
            EXPECT_EQ(output.packetBytes.data(), bytes.data());
            EXPECT_EQ(output.packetBytes.size(), bytes.size());
        }

        TEST(NrPacketParserTests, ParseCompletesOnlyFirstPacketWhenReadableHasMoreBytes)
        {
            const NrPacketParser parser = MakeParser();
            std::vector<std::byte> bytes;
            const std::array<std::byte, NrPacketHeaderLength> first = MakeHeaderBytes(6, 1);
            const std::array<std::byte, NrPacketHeaderLength> second = MakeHeaderBytes(6, 2);
            bytes.insert(bytes.end(), first.begin(), first.end());
            bytes.insert(bytes.end(), second.begin(), second.end());
            NrPacketParseResult output;

            const NrStatus status = parser.Parse(std::span<const std::byte>(bytes), output);

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(output.status, NrPacketParseStatus::Complete);
            EXPECT_EQ(output.header.packetType, 1u);
            EXPECT_EQ(output.packetBytes.data(), bytes.data());
            EXPECT_EQ(output.packetBytes.size(), NrPacketHeaderLength);
        }
    } // namespace

} // namespace psnr::core
