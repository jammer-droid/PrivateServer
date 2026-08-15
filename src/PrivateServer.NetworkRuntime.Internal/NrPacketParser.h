#pragma once

#include "NrResult.h"
#include "NrPacketHeader.h"
#include "NrProtocolErrorReason.h"
#include "NrStatus.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::core
{
    enum class NrPacketParseStatus
    {
        Complete,
        NeedMoreData,
        ProtocolError,
    };

    struct NrPacketParserConfig
    {
        std::size_t maxPacketSize = NrMaxPacketLength;
        std::uint8_t supportedVersion = NrCurrentProtocolVersion;
    };

    struct NrPacketParseResult
    {
        NrPacketParseStatus status = NrPacketParseStatus::NeedMoreData;
        NrProtocolErrorReason protocolErrorReason = NrProtocolErrorReason::None;
        NrPacketHeader header;
        std::span<const std::byte> packetBytes;
    };

    class NrPacketParser
    {
    public:
        [[nodiscard]] static NrResult<NrPacketParser> Create(const NrPacketParserConfig& config) noexcept;

        [[nodiscard]] NrStatus Parse(std::span<const std::byte> readableBytes,
                                     NrPacketParseResult& result) const noexcept;

    private:
        explicit NrPacketParser(const NrPacketParserConfig& config) noexcept;

    private:
        NrPacketParserConfig config_;
    };

} // namespace psnr::core
