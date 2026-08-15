#include "pch.h"

#include "NrPrimitiveCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::core
{
    namespace
    {
        TEST(NrPrimitiveCodecTests, ProtocolByteOrderDefaultsToLittleEndian)
        {
            EXPECT_EQ(NrDefaultByteOrder, NrByteOrder::LittleEndian);
        }

        TEST(NrPrimitiveCodecTests, ReadU8ReturnsSingleByteValue)
        {
            const std::array<std::byte, 1> bytes = {std::byte{0x7F}};

            EXPECT_EQ(NrPrimitiveCodec::ReadU8(std::span<const std::byte, 1>(bytes)), 0x7Fu);
        }

        TEST(NrPrimitiveCodecTests, ReadU16UsesProtocolByteOrder)
        {
            const std::array<std::byte, 2> bytes = {std::byte{0x34}, std::byte{0x12}};

            EXPECT_EQ(NrPrimitiveCodec::ReadU16(std::span<const std::byte, 2>(bytes)), 0x1234u);
        }

        TEST(NrPrimitiveCodecTests, ReadU16DoesNotMutateSourceBytes)
        {
            std::array<std::byte, 2> bytes = {std::byte{0x34}, std::byte{0x12}};

            const std::uint16_t value = NrPrimitiveCodec::ReadU16(std::span<const std::byte, 2>(bytes));

            EXPECT_EQ(value, 0x1234u);
            EXPECT_EQ(bytes[0], std::byte{0x34});
            EXPECT_EQ(bytes[1], std::byte{0x12});
        }

        TEST(NrPrimitiveCodecTests, WriteU8StoresSingleByteValue)
        {
            std::array<std::byte, 1> bytes{};

            NrPrimitiveCodec::WriteU8(0x7F, std::span<std::byte, 1>(bytes));

            EXPECT_EQ(bytes[0], std::byte{0x7F});
        }

        TEST(NrPrimitiveCodecTests, WriteU16UsesProtocolByteOrder)
        {
            std::array<std::byte, 2> bytes{};

            NrPrimitiveCodec::WriteU16(0x1234, std::span<std::byte, 2>(bytes));

            EXPECT_EQ(bytes[0], std::byte{0x34});
            EXPECT_EQ(bytes[1], std::byte{0x12});
        }
    } // namespace

} // namespace psnr::core
