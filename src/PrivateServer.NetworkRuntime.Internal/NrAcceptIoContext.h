#pragma once

#include "NrIocpIoContextHeader.h"
#include "NrIoOperationType.h"
#include "NrWin32Socket.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    // AcceptEx output buffer layout:
    // [initial recv bytes][local IPv4 sockaddr area][remote IPv4 sockaddr area]
    // [currently 0 bytes ][sizeof(sockaddr_in)+16 ][sizeof(sockaddr_in)+16  ]
    //
    // This context is sized for the current IPv4 listener socket.
    // Revisit this size when IPv6 listener sockets are introduced.
    // The extra 16 bytes per address area are required by AcceptEx. (MS docs)
    // Initial receive is disabled so first payload bytes flow through
    // the normal WSARecv path after session registration.
    inline constexpr std::size_t NrAcceptAddressLength = sizeof(sockaddr_in) + 16;
    inline constexpr std::size_t NrAcceptInitialReceiveLength = 0;
    inline constexpr std::size_t NrAcceptBufferLength = NrAcceptInitialReceiveLength + (NrAcceptAddressLength * 2);

    class NrAcceptIoContext final
    {
    public:
        NrAcceptIoContext() noexcept;

        [[nodiscard]] static NrAcceptIoContext* FromOverlapped(OVERLAPPED* overlapped) noexcept;

        [[nodiscard]] OVERLAPPED* Overlapped() noexcept;
        [[nodiscard]] NrIoOperationType Type() const noexcept;
        [[nodiscard]] NrWin32Socket& AcceptedSocket() noexcept;
        [[nodiscard]] NrWin32Socket TakeAcceptedSocket() noexcept;

        [[nodiscard]] void* Buffer() noexcept;
        [[nodiscard]] DWORD BufferLength() const noexcept;
        [[nodiscard]] DWORD InitialReceiveLength() const noexcept;
        [[nodiscard]] DWORD LocalAddressLength() const noexcept;
        [[nodiscard]] DWORD RemoteAddressLength() const noexcept;

        void ResetForPost() noexcept;

    private:
        // Must stay first. FromOverlapped() relies on header_ starting at the accept context address.
        NrIocpIoContextHeader header_;
        NrWin32Socket acceptedSocket_;
        std::array<std::uint8_t, NrAcceptBufferLength> buffer_{};
    };
} // namespace psnr::runtime
