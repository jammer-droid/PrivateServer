#include "pch.h"

#include "NrPacketHeaderCodec.h"

#include <array>
#include <cstddef>
#include <span>

namespace psnr::core
{
    namespace
    {
        TEST(NrPacketHeaderCodecTests, EncodeUsesProtocolWireLayout)
        {
            const NrPacketHeader header{0x1234, 0x5678, 0x9A, 0xBC};
            std::array<std::byte, NrPacketHeaderLength> bytes{};

            NrPacketHeaderCodec::Encode(header, std::span<std::byte, NrPacketHeaderLength>(bytes));

            const std::array<std::byte, NrPacketHeaderLength> expected = {
                std::byte{0x34}, std::byte{0x12}, std::byte{0x78},
                std::byte{0x56}, std::byte{0x9A}, std::byte{0xBC},
            };
            EXPECT_EQ(bytes, expected);
        }

        TEST(NrPacketHeaderCodecTests, EncodeAndDecodeRoundTrip)
        {
            const NrPacketHeader original{8192, 0xCAFE, 1, 2};
            std::array<std::byte, NrPacketHeaderLength> bytes{};

            NrPacketHeaderCodec::Encode(original, std::span<std::byte, NrPacketHeaderLength>(bytes));
            const NrPacketHeader decoded =
                NrPacketHeaderCodec::Decode(std::span<const std::byte, NrPacketHeaderLength>(bytes));

            EXPECT_EQ(decoded.packetLength, original.packetLength);
            EXPECT_EQ(decoded.packetType, original.packetType);
            EXPECT_EQ(decoded.version, original.version);
            EXPECT_EQ(decoded.flags, original.flags);
        }
    } // namespace
} // namespace psnr::core
