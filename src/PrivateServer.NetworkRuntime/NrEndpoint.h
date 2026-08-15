#pragma once

#include <cstdint>

namespace psnr::runtime
{
    enum class NrEndpointAddressType
    {
        IPv4,
    };

    struct NrIPv4Address final
    {
        std::uint8_t octets[4] = {127, 0, 0, 1};

        constexpr NrIPv4Address() noexcept = default;

        constexpr NrIPv4Address(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) noexcept
            : octets{a, b, c, d}
        {
        }

        [[nodiscard]] static constexpr NrIPv4Address Loopback() noexcept
        {
            return NrIPv4Address{127, 0, 0, 1};
        }

        [[nodiscard]] static constexpr NrIPv4Address Any() noexcept
        {
            return NrIPv4Address{0, 0, 0, 0};
        }
    };

    struct NrEndpoint final
    {
        NrEndpointAddressType addressType = NrEndpointAddressType::IPv4;
        NrIPv4Address ipv4Address = NrIPv4Address::Loopback();
        // NrIPv6Address ...
        std::uint16_t port = 0;
    };
} // namespace psnr::runtime
