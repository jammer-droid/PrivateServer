#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace psnr::core
{
    enum class NrByteOrder
    {
        LittleEndian,
        BigEndian
    };

    inline constexpr NrByteOrder NrDefaultByteOrder = NrByteOrder::LittleEndian;

    static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big,
                  "Unsupported mixed-endian platform");

    namespace detail
    {
        /*
        struct false_type
        {
            static constexpr bool value = false;
        };

        struct true_type
        {
            static constexpr bool value = true;
        };
        */

        // endian convert - default: false
        template <typename T> struct NrSupportsEndianConvert : std::false_type
        {
        };

        // supported endian convert type
        template <> struct NrSupportsEndianConvert<std::uint16_t> : std::true_type
        {
        };

        template <typename T> struct NrByteSwap;

        //
        template <> struct NrByteSwap<std::uint16_t>
        {
            [[nodiscard]] static constexpr std::uint16_t Convert(std::uint16_t value) noexcept
            {
                return static_cast<std::uint16_t>((value >> 8) | (value << 8));
            }
        };

        template <typename T, bool ShouldSwap> struct NrEndianConverter;

        // no convert
        template <typename T> struct NrEndianConverter<T, false>
        {
            [[nodiscard]] static constexpr T Convert(T value) noexcept
            {
                return value;
            }
        };

        // do convert
        template <typename T> struct NrEndianConverter<T, true>
        {
            [[nodiscard]] static constexpr T Convert(T value) noexcept
            {
                return NrByteSwap<T>::Convert(value);
            }
        };

        template <typename T> [[nodiscard]] constexpr T NrProtocolEndianConvert(T value) noexcept
        {
            static_assert(NrSupportsEndianConvert<T>::value, "Unsupported protocol endian conversion type");

            constexpr bool shouldSwap =
                (NrDefaultByteOrder == NrByteOrder::LittleEndian && std::endian::native == std::endian::big) ||
                (NrDefaultByteOrder == NrByteOrder::BigEndian && std::endian::native == std::endian::little);

            return NrEndianConverter<T, shouldSwap>::Convert(value);
        }
    } // namespace detail

    class NrPrimitiveCodec
    {
    public:
        [[nodiscard]] static std::uint8_t ReadU8(std::span<const std::byte, 1> bytes) noexcept
        {
            return static_cast<std::uint8_t>(bytes[0]);
        }

        [[nodiscard]] static std::uint16_t ReadU16(std::span<const std::byte, 2> bytes) noexcept
        {
            std::uint16_t value = 0;
            std::memcpy(&value, bytes.data(), sizeof(value));
            return detail::NrProtocolEndianConvert(value);
        }

        static void WriteU8(const std::uint8_t value, std::span<std::byte, 1> bytes) noexcept
        {
            bytes[0] = static_cast<std::byte>(value);
        }

        static void WriteU16(const std::uint16_t value, std::span<std::byte, 2> bytes) noexcept
        {
            const std::uint16_t encodedValue = detail::NrProtocolEndianConvert(value);
            std::memcpy(bytes.data(), &encodedValue, sizeof(encodedValue));
        }
    };

} // namespace psnr::core
