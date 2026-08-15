#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace psnr::benchmark
{
    inline constexpr std::uint16_t BenchmarkRequestPacketType = 0x7F01;
    inline constexpr std::uint16_t BenchmarkResponsePacketType = 0x7F02;
    inline constexpr std::uint16_t BenchmarkProtocolVersion = 2;

    inline constexpr std::size_t BenchmarkPayloadFieldBytes = 40;
    inline constexpr std::size_t BenchmarkCanonicalPayloadBytes = 64;
    inline constexpr std::size_t BenchmarkCanonicalPaddingBytes =
        BenchmarkCanonicalPayloadBytes - BenchmarkPayloadFieldBytes;

    enum class BenchmarkOperation : std::uint16_t
    {
        Echo = 1,
    };

    struct BenchmarkPayload final
    {
        std::uint16_t protocolVersion = BenchmarkProtocolVersion;
        BenchmarkOperation operation = BenchmarkOperation::Echo;
        std::uint32_t clientId = 0;
        std::uint64_t sequence = 0;
        std::uint64_t clientSendTimestampNanoseconds = 0;
        std::uint64_t serverReceivedTimestampNanoseconds = 0;
        std::uint64_t serverResponsePreparedTimestampNanoseconds = 0;

        [[nodiscard]] friend constexpr bool operator==(const BenchmarkPayload& left,
                                                       const BenchmarkPayload& right) noexcept = default;
    };

    class BenchmarkProtocolCodec final
    {
    public:
        using CanonicalPayload = std::array<std::byte, BenchmarkCanonicalPayloadBytes>;

        [[nodiscard]] static constexpr CanonicalPayload EncodeCanonical(const BenchmarkPayload& payload) noexcept
        {
            CanonicalPayload bytes{};
            WriteU16(payload.protocolVersion, ProtocolVersionOffset, &bytes);
            WriteU16(static_cast<std::uint16_t>(payload.operation), OperationOffset, &bytes);
            WriteU32(payload.clientId, ClientIdOffset, &bytes);
            WriteU64(payload.sequence, SequenceOffset, &bytes);
            WriteU64(payload.clientSendTimestampNanoseconds, ClientSendTimestampOffset, &bytes);
            WriteU64(payload.serverReceivedTimestampNanoseconds, ServerReceivedTimestampOffset, &bytes);
            WriteU64(payload.serverResponsePreparedTimestampNanoseconds, ServerResponsePreparedTimestampOffset,
                     &bytes);

            for (std::size_t paddingIndex = 0; paddingIndex < BenchmarkCanonicalPaddingBytes; ++paddingIndex)
            {
                bytes[BenchmarkPayloadFieldBytes + paddingIndex] = static_cast<std::byte>(paddingIndex);
            }
            return bytes;
        }

        [[nodiscard]] static constexpr BenchmarkPayload DecodeCanonical(const CanonicalPayload& bytes) noexcept
        {
            BenchmarkPayload payload;
            payload.protocolVersion = ReadU16(ProtocolVersionOffset, bytes);
            payload.operation = static_cast<BenchmarkOperation>(ReadU16(OperationOffset, bytes));
            payload.clientId = ReadU32(ClientIdOffset, bytes);
            payload.sequence = ReadU64(SequenceOffset, bytes);
            payload.clientSendTimestampNanoseconds = ReadU64(ClientSendTimestampOffset, bytes);
            payload.serverReceivedTimestampNanoseconds = ReadU64(ServerReceivedTimestampOffset, bytes);
            payload.serverResponsePreparedTimestampNanoseconds =
                ReadU64(ServerResponsePreparedTimestampOffset, bytes);
            return payload;
        }

        [[nodiscard]] static constexpr bool HasDeterministicPadding(const CanonicalPayload& bytes) noexcept
        {
            for (std::size_t paddingIndex = 0; paddingIndex < BenchmarkCanonicalPaddingBytes; ++paddingIndex)
            {
                if (bytes[BenchmarkPayloadFieldBytes + paddingIndex] != static_cast<std::byte>(paddingIndex))
                {
                    return false;
                }
            }
            return true;
        }

    private:
        inline static constexpr std::size_t ProtocolVersionOffset = 0;
        inline static constexpr std::size_t OperationOffset = 2;
        inline static constexpr std::size_t ClientIdOffset = 4;
        inline static constexpr std::size_t SequenceOffset = 8;
        inline static constexpr std::size_t ClientSendTimestampOffset = 16;
        inline static constexpr std::size_t ServerReceivedTimestampOffset = 24;
        inline static constexpr std::size_t ServerResponsePreparedTimestampOffset = 32;

        static constexpr void WriteU16(const std::uint16_t value, const std::size_t offset,
                                       CanonicalPayload* const outBytes) noexcept
        {
            for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
            {
                (*outBytes)[offset + byteIndex] = static_cast<std::byte>((value >> (byteIndex * 8)) & 0xFFu);
            }
        }

        static constexpr void WriteU32(const std::uint32_t value, const std::size_t offset,
                                       CanonicalPayload* const outBytes) noexcept
        {
            for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
            {
                (*outBytes)[offset + byteIndex] = static_cast<std::byte>((value >> (byteIndex * 8)) & 0xFFu);
            }
        }

        static constexpr void WriteU64(const std::uint64_t value, const std::size_t offset,
                                       CanonicalPayload* const outBytes) noexcept
        {
            for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
            {
                (*outBytes)[offset + byteIndex] = static_cast<std::byte>((value >> (byteIndex * 8)) & 0xFFu);
            }
        }

        [[nodiscard]] static constexpr std::uint16_t ReadU16(const std::size_t offset,
                                                             const CanonicalPayload& bytes) noexcept
        {
            std::uint16_t value = 0;
            for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
            {
                value |= static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[offset + byteIndex])
                                                    << (byteIndex * 8));
            }
            return value;
        }

        [[nodiscard]] static constexpr std::uint32_t ReadU32(const std::size_t offset,
                                                             const CanonicalPayload& bytes) noexcept
        {
            std::uint32_t value = 0;
            for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
            {
                value |= std::to_integer<std::uint32_t>(bytes[offset + byteIndex]) << (byteIndex * 8);
            }
            return value;
        }

        [[nodiscard]] static constexpr std::uint64_t ReadU64(const std::size_t offset,
                                                             const CanonicalPayload& bytes) noexcept
        {
            std::uint64_t value = 0;
            for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
            {
                value |= std::to_integer<std::uint64_t>(bytes[offset + byteIndex]) << (byteIndex * 8);
            }
            return value;
        }
    };
} // namespace psnr::benchmark
