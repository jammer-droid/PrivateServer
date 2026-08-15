#pragma once

#include "WorldProtocolValues.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace psnr::world::protocol
{
    class WorldProtocolWireCodec final
    {
    public:
        static void WriteU16(std::uint16_t value, std::size_t offset, std::span<std::byte> output) noexcept;
        [[nodiscard]] static std::uint16_t ReadU16(std::size_t offset, std::span<const std::byte> payload) noexcept;

        static void WriteU32(std::uint32_t value, std::size_t offset, std::span<std::byte> output) noexcept;
        [[nodiscard]] static std::uint32_t ReadU32(std::size_t offset, std::span<const std::byte> payload) noexcept;

        static void WriteI16(std::int16_t value, std::size_t offset, std::span<std::byte> output) noexcept;
        [[nodiscard]] static std::int16_t ReadI16(std::size_t offset, std::span<const std::byte> payload) noexcept;

        static void WriteF32(float value, std::size_t offset, std::span<std::byte> output) noexcept;
        [[nodiscard]] static float ReadF32(std::size_t offset, std::span<const std::byte> payload) noexcept;

        [[nodiscard]] static bool IsValidPlayerDisplayName(std::string_view value) noexcept;
        [[nodiscard]] static bool IsValidPlayerDisplayName(std::span<const std::byte> value) noexcept;

        static constexpr std::size_t MaximumPlayerDisplayNameBytes = 48;
    };
} // namespace psnr::world::protocol
