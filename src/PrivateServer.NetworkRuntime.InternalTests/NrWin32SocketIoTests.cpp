#include "pch.h"

#include "NrAcceptIoContext.h"
#include "NrClientConnectIoContext.h"
#include "NrEndpoint.h"
#include "NrErrorCode.h"
#include "NrIocpPort.h"
#include "NrSocketAddressWin32.h"
#include "NrWin32Socket.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    namespace
    {
        class NrTestWinsockRuntime final
        {
        public:
            NrTestWinsockRuntime() noexcept
            {
                WSADATA data{};
                startResult_ = WSAStartup(MAKEWORD(2, 2), &data);
            }

            NrTestWinsockRuntime(const NrTestWinsockRuntime&) = delete;
            NrTestWinsockRuntime& operator=(const NrTestWinsockRuntime&) = delete;

            ~NrTestWinsockRuntime() noexcept
            {
                if (startResult_ == 0)
                {
                    static_cast<void>(WSACleanup());
                }
            }

            [[nodiscard]] bool Started() const noexcept
            {
                return startResult_ == 0;
            }

        private:
            int startResult_ = SOCKET_ERROR;
        };

        class NrTestSocket final
        {
        public:
            explicit NrTestSocket(const SOCKET socket) noexcept
                : socket_(socket)
            {
            }

            NrTestSocket(const NrTestSocket&) = delete;
            NrTestSocket& operator=(const NrTestSocket&) = delete;

            ~NrTestSocket() noexcept
            {
                if (socket_ != INVALID_SOCKET)
                {
                    static_cast<void>(closesocket(socket_));
                }
            }

            [[nodiscard]] SOCKET Get() const noexcept
            {
                return socket_;
            }

        private:
            SOCKET socket_ = INVALID_SOCKET;
        };

        [[nodiscard]] bool SendAll(const SOCKET socket, const char* bytes, const int length) noexcept
        {
            int totalBytesSent = 0;
            while (totalBytesSent < length)
            {
                const int bytesSent = send(socket, bytes + totalBytesSent, length - totalBytesSent, 0);
                if (bytesSent == SOCKET_ERROR || bytesSent == 0)
                {
                    return false;
                }

                totalBytesSent += bytesSent;
            }

            return true;
        }

        [[nodiscard]] bool RecvAll(const SOCKET socket, char* bytes, const int length) noexcept
        {
            int totalBytesReceived = 0;
            while (totalBytesReceived < length)
            {
                const int bytesReceived = recv(socket, bytes + totalBytesReceived, length - totalBytesReceived, 0);
                if (bytesReceived == SOCKET_ERROR || bytesReceived == 0)
                {
                    return false;
                }

                totalBytesReceived += bytesReceived;
            }

            return true;
        }

        TEST(NrWin32SocketIoTests, ConnectedSocketPostsOverlappedRecvAndSend)
        {
            NrTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            std::array<std::byte, 4> recvStorage{};
            WSABUF recvBuffer{};
            recvBuffer.buf = reinterpret_cast<char*>(recvStorage.data());
            recvBuffer.len = static_cast<ULONG>(recvStorage.size());
            OVERLAPPED recvOverlapped{};

            std::array<char, 4> sendStorage = {'s', 'e', 'n', 'd'};
            WSABUF sendBuffer{};
            sendBuffer.buf = sendStorage.data();
            sendBuffer.len = static_cast<ULONG>(sendStorage.size());
            OVERLAPPED sendOverlapped{};

            NrAcceptIoContext acceptContext;

            NrWin32Socket listenSocket;
            ASSERT_TRUE(listenSocket.OpenOverlappedTcpIPv4().Succeeded());
            const NrEndpoint listenEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), 0};
            ASSERT_TRUE(listenSocket.Bind(listenEndpoint).Succeeded());
            ASSERT_TRUE(listenSocket.Listen(1).Succeeded());
            ASSERT_TRUE(listenSocket.LoadAcceptEx().Succeeded());

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            ASSERT_TRUE(iocpPort.AssociateSocket(listenSocket, 1).Succeeded());

            sockaddr_in boundAddress{};
            int boundAddressLength = sizeof(boundAddress);
            ASSERT_NE(getsockname(listenSocket.NativeSocket(), reinterpret_cast<sockaddr*>(&boundAddress),
                                  &boundAddressLength),
                      SOCKET_ERROR);

            ASSERT_TRUE(acceptContext.AcceptedSocket().OpenOverlappedTcpIPv4().Succeeded());
            ASSERT_TRUE(listenSocket
                            .PostAccept(acceptContext.AcceptedSocket(), acceptContext.Buffer(),
                                        acceptContext.InitialReceiveLength(), acceptContext.LocalAddressLength(),
                                        acceptContext.RemoteAddressLength(), acceptContext.Overlapped())
                            .Succeeded());

            NrTestSocket clientSocket(WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
            ASSERT_NE(clientSocket.Get(), INVALID_SOCKET);
            ASSERT_NE(connect(clientSocket.Get(), reinterpret_cast<sockaddr*>(&boundAddress), sizeof(boundAddress)),
                      SOCKET_ERROR);

            NrIocpCompletionPacket acceptPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(acceptPacket).Succeeded());
            ASSERT_TRUE(acceptPacket.ioStatus.Succeeded());
            EXPECT_EQ(acceptPacket.completionKey, 1u);
            EXPECT_EQ(acceptPacket.overlapped, acceptContext.Overlapped());
            ASSERT_TRUE(acceptContext.AcceptedSocket().UpdateAcceptContext(listenSocket).Succeeded());
            ASSERT_TRUE(iocpPort.AssociateSocket(acceptContext.AcceptedSocket(), 2).Succeeded());

            ASSERT_TRUE(acceptContext.AcceptedSocket().PostRecv(recvBuffer, &recvOverlapped).Succeeded());
            const std::array<char, 4> clientSendBytes = {'r', 'e', 'c', 'v'};
            ASSERT_TRUE(SendAll(clientSocket.Get(), clientSendBytes.data(), static_cast<int>(clientSendBytes.size())));

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(recvPacket.ioStatus.Succeeded());
            EXPECT_EQ(recvPacket.completionKey, 2u);
            EXPECT_EQ(recvPacket.overlapped, &recvOverlapped);
            const DWORD recvBytes = recvPacket.bytesTransferred;
            ASSERT_GT(recvBytes, 0u);
            ASSERT_LE(recvBytes, recvStorage.size());
            EXPECT_EQ(recvStorage[0], static_cast<std::byte>('r'));
            for (DWORD index = 0; index < recvBytes; ++index)
            {
                EXPECT_EQ(recvStorage[index], static_cast<std::byte>(clientSendBytes[index]));
            }

            ASSERT_TRUE(acceptContext.AcceptedSocket().PostSend(sendBuffer, &sendOverlapped).Succeeded());
            NrIocpCompletionPacket sendPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(sendPacket).Succeeded());
            ASSERT_TRUE(sendPacket.ioStatus.Succeeded());
            EXPECT_EQ(sendPacket.completionKey, 2u);
            EXPECT_EQ(sendPacket.overlapped, &sendOverlapped);
            const DWORD sentBytes = sendPacket.bytesTransferred;
            ASSERT_GT(sentBytes, 0u);
            ASSERT_LE(sentBytes, sendStorage.size());

            std::array<char, 4> clientRecvBytes{};
            ASSERT_TRUE(RecvAll(clientSocket.Get(), clientRecvBytes.data(), static_cast<int>(sentBytes)));
            for (DWORD index = 0; index < sentBytes; ++index)
            {
                EXPECT_EQ(clientRecvBytes[index], sendStorage[index]);
            }
        }

        TEST(NrWin32SocketIoTests, NonConnectedSocketRejectsRecvAndSendPosts)
        {
            NrWin32Socket socket;
            std::array<char, 1> storage{};
            WSABUF buffer{};
            buffer.buf = storage.data();
            buffer.len = static_cast<ULONG>(storage.size());
            OVERLAPPED overlapped{};

            EXPECT_EQ(socket.PostRecv(buffer, &overlapped).ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(socket.PostSend(buffer, &overlapped).ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        }

        TEST(NrWin32SocketIoTests, ConnectExCompletesThroughAssociatedIocpPort)
        {
            NrTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            ASSERT_TRUE(listenSocket.OpenOverlappedTcpIPv4().Succeeded());
            const NrEndpoint listenEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), 0};
            ASSERT_TRUE(listenSocket.Bind(listenEndpoint).Succeeded());
            ASSERT_TRUE(listenSocket.Listen(1).Succeeded());

            sockaddr_in boundAddress{};
            int boundAddressLength = sizeof(boundAddress);
            ASSERT_NE(getsockname(listenSocket.NativeSocket(), reinterpret_cast<sockaddr*>(&boundAddress),
                                  &boundAddressLength),
                      SOCKET_ERROR);

            NrSocketAddressWin32 remoteAddress;
            remoteAddress.address = boundAddress;
            internal::NrClientConnectIoContext connectContext(5, remoteAddress);

            NrWin32Socket clientSocket;
            ASSERT_TRUE(clientSocket.OpenOverlappedTcpIPv4().Succeeded());
            EXPECT_EQ(clientSocket.LoadConnectEx().ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            const NrEndpoint localEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Any(), 0};
            ASSERT_TRUE(clientSocket.Bind(localEndpoint).Succeeded());

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            ASSERT_TRUE(iocpPort.AssociateSocket(clientSocket, 7).Succeeded());
            ASSERT_TRUE(clientSocket.LoadConnectEx().Succeeded());

            ASSERT_TRUE(clientSocket
                            .PostConnect(connectContext.RemoteAddress(), connectContext.RemoteAddressLength(),
                                         connectContext.Overlapped())
                            .Succeeded());
            EXPECT_EQ(clientSocket.State(), NrWin32SocketState::ConnectPendingTcpIPv4);

            NrIocpCompletionPacket connectPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(connectPacket).Succeeded());
            ASSERT_TRUE(connectPacket.ioStatus.Succeeded());
            EXPECT_EQ(connectPacket.completionKey, 7u);
            EXPECT_EQ(connectPacket.overlapped, connectContext.Overlapped());

            ASSERT_TRUE(clientSocket.UpdateConnectContext().Succeeded());
            EXPECT_EQ(clientSocket.State(), NrWin32SocketState::ConnectedTcpIPv4);

            NrTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
        }
    } // namespace
} // namespace psnr::runtime
