#include "pch.h"

#include "NrWin32Socket.h"

#include "NrErrorCode.h"
#include "NrSocketAddressWin32.h"

#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    namespace
    {
        [[nodiscard]] NrStatus LastSocketErrorStatus() noexcept
        {
            return NrStatus::Failure(NrErrorCode::IoFailed,
                                     static_cast<psnr::core::NrNativeErrorCode>(WSAGetLastError()));
        }

        [[nodiscard]] NrStatus OverlappedPostStatus(const int result) noexcept
        {
            if (result != SOCKET_ERROR)
            {
                return NrStatus::Success();
            }

            const int socketError = WSAGetLastError();
            if (socketError == WSA_IO_PENDING)
            {
                return NrStatus::Success();
            }

            return NrStatus::Failure(NrErrorCode::IoFailed, static_cast<psnr::core::NrNativeErrorCode>(socketError));
        }
    } // namespace

    NrWin32Socket::~NrWin32Socket() noexcept
    {
        (void)Close();
    }

    NrWin32Socket::NrWin32Socket(NrWin32Socket&& other) noexcept
        : socket_(std::exchange(other.socket_, INVALID_SOCKET))
        , connectEx_(std::exchange(other.connectEx_, nullptr))
        , acceptEx_(std::exchange(other.acceptEx_, nullptr))
        , state_(std::exchange(other.state_, NrWin32SocketState::Closed))
    {
    }

    NrWin32Socket& NrWin32Socket::operator=(NrWin32Socket&& other) noexcept
    {
        if (this != &other)
        {
            (void)Close();
            socket_ = std::exchange(other.socket_, INVALID_SOCKET);
            connectEx_ = std::exchange(other.connectEx_, nullptr);
            acceptEx_ = std::exchange(other.acceptEx_, nullptr);
            state_ = std::exchange(other.state_, NrWin32SocketState::Closed);
        }

        return *this;
    }

    bool NrWin32Socket::IsValid() const noexcept
    {
        return socket_ != INVALID_SOCKET;
    }

    NrWin32SocketState NrWin32Socket::State() const noexcept
    {
        return state_;
    }

    SOCKET NrWin32Socket::NativeSocket() const noexcept
    {
        return socket_;
    }

    NrStatus NrWin32Socket::OpenOverlappedTcpIPv4() noexcept
    {
        if (IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (socket == INVALID_SOCKET)
        {
            return LastSocketErrorStatus();
        }

        socket_ = socket;
        state_ = NrWin32SocketState::OpenTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::Bind(const NrEndpoint& endpoint) noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::OpenTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrSocketAddressWin32 socketAddress;
        const NrStatus addressStatus = BuildSocketAddressWin32(endpoint, socketAddress);
        if (addressStatus.Failed())
        {
            return addressStatus;
        }

        const int result =
            bind(socket_, reinterpret_cast<const sockaddr*>(&socketAddress.address), socketAddress.length);
        if (result == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        state_ = NrWin32SocketState::BoundTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::Listen(int backlog) noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::BoundTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (listen(socket_, backlog) == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        state_ = NrWin32SocketState::ListeningTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::LoadConnectEx() noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::BoundTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        GUID connectExId = WSAID_CONNECTEX;
        LPFN_CONNECTEX loadedConnectEx = nullptr;
        DWORD bytesReturned = 0;

        const int result = WSAIoctl(socket_, SIO_GET_EXTENSION_FUNCTION_POINTER, &connectExId, sizeof(connectExId),
                                    &loadedConnectEx, sizeof(loadedConnectEx), &bytesReturned, nullptr, nullptr);
        if (result == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        if (loadedConnectEx == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::IoFailed);
        }

        connectEx_ = loadedConnectEx;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::PostConnect(const sockaddr* remoteAddress, const int remoteAddressLength,
                                        OVERLAPPED* overlapped) noexcept
    {
        if (!IsValid() || connectEx_ == nullptr || state_ != NrWin32SocketState::BoundTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (remoteAddress == nullptr || remoteAddressLength <= 0 || overlapped == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        DWORD bytesSent = 0;
        const BOOL result = connectEx_(socket_, remoteAddress, remoteAddressLength, nullptr, 0, &bytesSent, overlapped);
        if (result == FALSE)
        {
            const int socketError = WSAGetLastError();
            if (socketError != WSA_IO_PENDING)
            {
                return NrStatus::Failure(NrErrorCode::IoFailed,
                                         static_cast<psnr::core::NrNativeErrorCode>(socketError));
            }
        }

        state_ = NrWin32SocketState::ConnectPendingTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::UpdateConnectContext() noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::ConnectPendingTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (setsockopt(socket_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        state_ = NrWin32SocketState::ConnectedTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::LoadAcceptEx() noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::ListeningTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        GUID acceptExId = WSAID_ACCEPTEX;       // winsock extension function - AcceptEx
        LPFN_ACCEPTEX loadedAcceptEx = nullptr; // AcceptEx function pointer
        DWORD bytesReturned = 0;

        const int result = WSAIoctl(socket_, SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptExId, sizeof(acceptExId),
                                    &loadedAcceptEx, sizeof(loadedAcceptEx), &bytesReturned, nullptr, nullptr);
        if (result == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        if (loadedAcceptEx == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::IoFailed);
        }

        acceptEx_ = loadedAcceptEx;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::PostAccept(NrWin32Socket& acceptedSocket, void* buffer, DWORD initialReceiveLength,
                                       DWORD localAddressLength, DWORD remoteAddressLength,
                                       OVERLAPPED* overlapped) noexcept
    {
        if (!IsValid() || acceptEx_ == nullptr || state_ != NrWin32SocketState::ListeningTcpIPv4 ||
            !acceptedSocket.IsValid() || acceptedSocket.state_ != NrWin32SocketState::OpenTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (buffer == nullptr || overlapped == nullptr || localAddressLength == 0 || remoteAddressLength == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        DWORD bytesReceived = 0;
        const BOOL result = acceptEx_(socket_, acceptedSocket.NativeSocket(), buffer, initialReceiveLength,
                                      localAddressLength, remoteAddressLength, &bytesReceived, overlapped);
        if (result == FALSE)
        {
            const int socketError = WSAGetLastError();
            if (socketError != WSA_IO_PENDING)
            {
                return NrStatus::Failure(NrErrorCode::IoFailed,
                                         static_cast<psnr::core::NrNativeErrorCode>(socketError));
            }
        }

        acceptedSocket.state_ = NrWin32SocketState::AcceptPendingTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::UpdateAcceptContext(const NrWin32Socket& listenSocket) noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::AcceptPendingTcpIPv4 || !listenSocket.IsValid() ||
            listenSocket.state_ != NrWin32SocketState::ListeningTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const SOCKET listenNativeSocket = listenSocket.socket_;

        // Apply the listen socket context required after AcceptEx completion.
        const int result = setsockopt(socket_, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                                      reinterpret_cast<const char*>(&listenNativeSocket), sizeof(listenNativeSocket));
        if (result == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        state_ = NrWin32SocketState::ConnectedTcpIPv4;
        return NrStatus::Success();
    }

    NrStatus NrWin32Socket::PostRecv(WSABUF& buffer, OVERLAPPED* overlapped) noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::ConnectedTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (buffer.buf == nullptr || buffer.len == 0 || overlapped == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        DWORD flags = 0;
        return OverlappedPostStatus(WSARecv(socket_, &buffer, 1, nullptr, &flags, overlapped, nullptr));
    }

    NrStatus NrWin32Socket::PostSend(WSABUF& buffer, OVERLAPPED* overlapped) noexcept
    {
        if (!IsValid() || state_ != NrWin32SocketState::ConnectedTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (buffer.buf == nullptr || buffer.len == 0 || overlapped == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return OverlappedPostStatus(WSASend(socket_, &buffer, 1, nullptr, 0, overlapped, nullptr));
    }

    NrStatus NrWin32Socket::Close() noexcept
    {
        if (!IsValid())
        {
            return NrStatus::Success();
        }

        const SOCKET socket = socket_;
        socket_ = INVALID_SOCKET;
        connectEx_ = nullptr;
        acceptEx_ = nullptr;
        state_ = NrWin32SocketState::Closed;

        if (closesocket(socket) == SOCKET_ERROR)
        {
            return LastSocketErrorStatus();
        }

        return NrStatus::Success();
    }
} // namespace psnr::runtime
