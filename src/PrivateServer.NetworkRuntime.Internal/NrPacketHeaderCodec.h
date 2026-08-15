#pragma once

#include "NrPacketHeader.h"
#include "NrPrimitiveCodec.h"

#include <cstddef>
#include <span>

namespace psnr::core
{
    class NrPacketHeaderCodec
    {
    public:
        static void Encode(const NrPacketHeader& header,
                           std::span<std::byte, NrPacketHeaderLength> bytes) noexcept
        {
            NrPrimitiveCodec::WriteU16(header.packetLength, bytes.subspan<0, 2>());
            NrPrimitiveCodec::WriteU16(header.packetType, bytes.subspan<2, 2>());
            NrPrimitiveCodec::WriteU8(header.version, bytes.subspan<4, 1>());
            NrPrimitiveCodec::WriteU8(header.flags, bytes.subspan<5, 1>());
        }

        [[nodiscard]] static NrPacketHeader Decode(std::span<const std::byte, NrPacketHeaderLength> bytes) noexcept
        {
            NrPacketHeader header;
            header.packetLength = NrPrimitiveCodec::ReadU16(bytes.subspan<0, 2>());
            header.packetType = NrPrimitiveCodec::ReadU16(bytes.subspan<2, 2>());
            header.version = NrPrimitiveCodec::ReadU8(bytes.subspan<4, 1>());
            header.flags = NrPrimitiveCodec::ReadU8(bytes.subspan<5, 1>());
            return header;
        }
    };

} // namespace psnr::core
