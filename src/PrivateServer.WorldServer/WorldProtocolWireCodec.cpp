#include "pch.h"

#include "WorldProtocolWireCodec.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace psnr::world::protocol
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);

    void WorldProtocolWireCodec::WriteU16(const std::uint16_t value, const std::size_t offset,
                                          const std::span<std::byte> output) noexcept
    {
        output[offset] = static_cast<std::byte>(value & 0xFFu);
        output[offset + 1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
    }

    std::uint16_t WorldProtocolWireCodec::ReadU16(const std::size_t offset,
                                                  const std::span<const std::byte> payload) noexcept
    {
        const std::uint16_t low = std::to_integer<std::uint16_t>(payload[offset]);
        const std::uint16_t high = std::to_integer<std::uint16_t>(payload[offset + 1]);
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8u));
    }

    void WorldProtocolWireCodec::WriteU32(const std::uint32_t value, const std::size_t offset,
                                          const std::span<std::byte> output) noexcept
    {
        output[offset] = static_cast<std::byte>(value & 0xFFu);
        output[offset + 1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
        output[offset + 2] = static_cast<std::byte>((value >> 16u) & 0xFFu);
        output[offset + 3] = static_cast<std::byte>((value >> 24u) & 0xFFu);
    }

    std::uint32_t WorldProtocolWireCodec::ReadU32(const std::size_t offset,
                                                  const std::span<const std::byte> payload) noexcept
    {
        const std::uint32_t byte0 = std::to_integer<std::uint32_t>(payload[offset]);
        const std::uint32_t byte1 = std::to_integer<std::uint32_t>(payload[offset + 1]);
        const std::uint32_t byte2 = std::to_integer<std::uint32_t>(payload[offset + 2]);
        const std::uint32_t byte3 = std::to_integer<std::uint32_t>(payload[offset + 3]);
        return byte0 | (byte1 << 8u) | (byte2 << 16u) | (byte3 << 24u);
    }

    void WorldProtocolWireCodec::WriteI16(const std::int16_t value, const std::size_t offset,
                                          const std::span<std::byte> output) noexcept
    {
        WriteU16(static_cast<std::uint16_t>(value), offset, output);
    }

    std::int16_t WorldProtocolWireCodec::ReadI16(const std::size_t offset,
                                                 const std::span<const std::byte> payload) noexcept
    {
        const std::uint16_t encodedValue = ReadU16(offset, payload);
        if (encodedValue <= static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max()))
        {
            return static_cast<std::int16_t>(encodedValue);
        }

        const std::int32_t signedValue = static_cast<std::int32_t>(encodedValue) - 0x1'0000;
        return static_cast<std::int16_t>(signedValue);
    }

    void WorldProtocolWireCodec::WriteF32(const float value, const std::size_t offset,
                                          const std::span<std::byte> output) noexcept
    {
        const float normalizedValue = value == 0.0f ? 0.0f : value;
        WriteU32(std::bit_cast<std::uint32_t>(normalizedValue), offset, output);
    }

    float WorldProtocolWireCodec::ReadF32(const std::size_t offset, const std::span<const std::byte> payload) noexcept
    {
        const float value = std::bit_cast<float>(ReadU32(offset, payload));
        return value == 0.0f ? 0.0f : value;
    }

    bool WorldProtocolWireCodec::IsValidPlayerDisplayName(const std::string_view value) noexcept
    {
        if (value.size() > MaximumPlayerDisplayNameBytes)
        {
            return false;
        }
        for (const char character : value)
        {
            const bool isUppercase = character >= 'A' && character <= 'Z';
            const bool isLowercase = character >= 'a' && character <= 'z';
            const bool isDigit = character >= '0' && character <= '9';
            if (!isUppercase && !isLowercase && !isDigit)
            {
                return false;
            }
        }
        return true;
    }

    bool WorldProtocolWireCodec::IsValidPlayerDisplayName(const std::span<const std::byte> value) noexcept
    {
        if (value.size() > MaximumPlayerDisplayNameBytes)
        {
            return false;
        }
        for (const std::byte character : value)
        {
            const std::uint8_t byteValue = std::to_integer<std::uint8_t>(character);
            const bool isUppercase = byteValue >= static_cast<std::uint8_t>('A') &&
                                     byteValue <= static_cast<std::uint8_t>('Z');
            const bool isLowercase = byteValue >= static_cast<std::uint8_t>('a') &&
                                     byteValue <= static_cast<std::uint8_t>('z');
            const bool isDigit = byteValue >= static_cast<std::uint8_t>('0') &&
                                 byteValue <= static_cast<std::uint8_t>('9');
            if (!isUppercase && !isLowercase && !isDigit)
            {
                return false;
            }
        }
        return true;
    }
} // namespace psnr::world::protocol
