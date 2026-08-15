#pragma once

#include "NrEndpoint.h"
#include "NrStatus.h"
#include "NrWindows.h"

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    enum class NrWin32SocketState
    {
        Closed,
        OpenTcpIPv4,
        BoundTcpIPv4,
        ConnectPendingTcpIPv4,
        ListeningTcpIPv4,
        AcceptPendingTcpIPv4,
        ConnectedTcpIPv4,
    };

    class NrWin32Socket final
    {
    public:
        NrWin32Socket() noexcept = default;

        NrWin32Socket(const NrWin32Socket&) = delete;
        NrWin32Socket& operator=(const NrWin32Socket&) = delete;

        NrWin32Socket(NrWin32Socket&& other) noexcept;
        NrWin32Socket& operator=(NrWin32Socket&& other) noexcept;

        ~NrWin32Socket() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] NrWin32SocketState State() const noexcept;
        [[nodiscard]] NrStatus OpenOverlappedTcpIPv4() noexcept;
        [[nodiscard]] NrStatus Bind(const NrEndpoint& endpoint) noexcept;
        [[nodiscard]] NrStatus Listen(int backlog) noexcept;

        [[nodiscard]] NrStatus LoadConnectEx() noexcept;
        [[nodiscard]] NrStatus PostConnect(const sockaddr* remoteAddress, int remoteAddressLength,
                                           OVERLAPPED* overlapped) noexcept;
        [[nodiscard]] NrStatus UpdateConnectContext() noexcept;
        [[nodiscard]] NrStatus LoadAcceptEx() noexcept;
        [[nodiscard]] NrStatus PostAccept(NrWin32Socket& acceptedSocket, void* buffer, DWORD initialReceiveLength,
                                          DWORD localAddressLength, DWORD remoteAddressLength,
                                          OVERLAPPED* overlapped) noexcept;
        [[nodiscard]] NrStatus UpdateAcceptContext(const NrWin32Socket& listenSocket) noexcept;
        [[nodiscard]] NrStatus PostRecv(WSABUF& buffer, OVERLAPPED* overlapped) noexcept;
        [[nodiscard]] NrStatus PostSend(WSABUF& buffer, OVERLAPPED* overlapped) noexcept;
        [[nodiscard]] NrStatus Close() noexcept;

        [[nodiscard]] SOCKET NativeSocket() const noexcept;

    private:
        SOCKET socket_ = INVALID_SOCKET;
        LPFN_CONNECTEX connectEx_ = nullptr;
        LPFN_ACCEPTEX acceptEx_ = nullptr;
        NrWin32SocketState state_ = NrWin32SocketState::Closed;
    };
} // namespace psnr::runtime
