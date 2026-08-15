#pragma once

#include <cstddef>
#include <cstdint>

namespace psnr::core
{
    inline constexpr std::size_t NrPacketHeaderLength = 6;
    inline constexpr std::size_t NrMaxPacketLength = 8192;
    inline constexpr std::uint8_t NrCurrentProtocolVersion = 1;

    struct NrPacketHeader
    {
        std::uint16_t packetLength = 0;
        std::uint16_t packetType = 0;
        std::uint8_t version = 0;
        std::uint8_t flags = 0;
    };

} // namespace psnr::core
