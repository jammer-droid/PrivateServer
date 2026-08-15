#include "pch.h"

#include "NrSocketAddressWin32.h"

namespace psnr::runtime
{
    NrStatus BuildSocketAddressWin32(const NrEndpoint& endpoint, NrSocketAddressWin32& outAddress) noexcept
    {
        sockaddr_in address{};                   // winsock IPv4 socket address
        address.sin_family = AF_INET;            // IPv4 address
        address.sin_port = htons(endpoint.port); // host to network-short
        address.sin_addr.S_un.S_un_b.s_b1 = endpoint.ipv4Address.octets[0];
        address.sin_addr.S_un.S_un_b.s_b2 = endpoint.ipv4Address.octets[1];
        address.sin_addr.S_un.S_un_b.s_b3 = endpoint.ipv4Address.octets[2];
        address.sin_addr.S_un.S_un_b.s_b4 = endpoint.ipv4Address.octets[3];

        outAddress.address = address;
        outAddress.length = sizeof(sockaddr_in);
        return NrStatus::Success();
    }
} // namespace psnr::runtime
