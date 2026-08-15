#include "pch.h"

#include "NrPacketParser.h"

#include "NrPacketHeaderCodec.h"

namespace psnr::core
{
    namespace
    {
        [[nodiscard]] NrStatus SetProtocolError(NrPacketParseResult& result,
                                                const NrProtocolErrorReason reason) noexcept
        {
            result.status = NrPacketParseStatus::ProtocolError;
            result.protocolErrorReason = reason;
            return NrStatus(NrErrorCode::ProtocolError);
        }
    } // namespace

    NrResult<NrPacketParser> NrPacketParser::Create(const NrPacketParserConfig& config) noexcept
    {
        if (config.maxPacketSize < NrPacketHeaderLength)
        {
            return NrResult<NrPacketParser>::Failure(NrErrorCode::InvalidArgument);
        }

        if (config.supportedVersion == 0)
        {
            return NrResult<NrPacketParser>::Failure(NrErrorCode::InvalidArgument);
        }

        return NrResult<NrPacketParser>(NrPacketParser(config));
    }

    NrStatus NrPacketParser::Parse(std::span<const std::byte> readableBytes, NrPacketParseResult& result) const noexcept
    {
        result = NrPacketParseResult{};

        if (readableBytes.size() < NrPacketHeaderLength)
        {
            return NrStatus();
        }

        const NrPacketHeader header = NrPacketHeaderCodec::Decode(
            std::span<const std::byte, NrPacketHeaderLength>(readableBytes.data(), NrPacketHeaderLength));

        result.header = header;

        if (header.packetLength < NrPacketHeaderLength)
        {
            return SetProtocolError(result, NrProtocolErrorReason::InvalidLength);
        }

        if (header.packetLength > config_.maxPacketSize)
        {
            return SetProtocolError(result, NrProtocolErrorReason::PacketTooLarge);
        }

        if (header.version != config_.supportedVersion)
        {
            return SetProtocolError(result, NrProtocolErrorReason::UnsupportedVersion);
        }

        if (header.flags != 0)
        {
            return SetProtocolError(result, NrProtocolErrorReason::ReservedFlags);
        }

        if (readableBytes.size() < header.packetLength) // recv로 들어온 bytes 가 아직 consume 할만큼 충분하지 않은 상태
        {
            result = NrPacketParseResult{};
            return NrStatus();
        }

        result.status = NrPacketParseStatus::Complete;
        result.protocolErrorReason = NrProtocolErrorReason::None;
        result.packetBytes = readableBytes.first(header.packetLength);
        return NrStatus();
    }

    NrPacketParser::NrPacketParser(const NrPacketParserConfig& config) noexcept
        : config_(config)
    {
    }

} // namespace psnr::core
