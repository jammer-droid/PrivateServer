#include "pch.h"

#include "NrClientConnection.h"
#include "NrEndpoint.h"
#include "NrErrorCode.h"
#include "NrIocpCompletionPacket.h"
#include "NrIocpPort.h"
#include "NrMemoryPoolTestUtils.h"
#include "NrPacketHeader.h"
#include "NrPayloadRef.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrSocketAddressWin32.h"
#include "NrWin32Socket.h"
#include "NrWindows.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

namespace psnr::runtime::internal
{
    namespace
    {
        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateClientConnectionMemoryPool(
            const std::size_t contextBlockCount = 2)
        {
            psnr::core::NrMemoryPoolManagerConfig config =
                psnr::core::test::MakeDefaultMemoryPoolManagerConfig();
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::RecvBuffer,
                psnr::core::test::MakePoolConfig(psnr::core::NrMaxPacketLength)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::OverlappedContext,
                psnr::core::test::MakePoolConfig(std::max(sizeof(NrRecvIoContext), sizeof(NrSendIoContext)),
                                                 contextBlockCount)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::Payload64,
                psnr::core::test::MakePoolConfig(64)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::PayloadRefControl,
                psnr::core::test::MakePoolConfig(64)));
            return psnr::core::test::CreateMemoryPoolManager(config);
        }

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

        [[nodiscard]] bool ReceiveAll(const SOCKET socket, char* bytes, const int length) noexcept
        {
            int totalBytesReceived = 0;
            while (totalBytesReceived < length)
            {
                const int bytesReceived = recv(socket, bytes + totalBytesReceived,
                                               length - totalBytesReceived, 0);
                if (bytesReceived == SOCKET_ERROR || bytesReceived == 0)
                {
                    return false;
                }

                totalBytesReceived += bytesReceived;
            }

            return true;
        }

        class NrClientConnectionTestWinsockRuntime final
        {
        public:
            NrClientConnectionTestWinsockRuntime() noexcept
            {
                WSADATA data{};
                startResult_ = WSAStartup(MAKEWORD(2, 2), &data);
            }

            NrClientConnectionTestWinsockRuntime(const NrClientConnectionTestWinsockRuntime&) = delete;
            NrClientConnectionTestWinsockRuntime& operator=(const NrClientConnectionTestWinsockRuntime&) = delete;

            ~NrClientConnectionTestWinsockRuntime() noexcept
            {
                if (startResult_ == 0)
                {
                    (void)WSACleanup();
                }
            }

            [[nodiscard]] bool Started() const noexcept
            {
                return startResult_ == 0;
            }

        private:
            int startResult_ = SOCKET_ERROR;
        };

        class NrClientConnectionTestSocket final
        {
        public:
            explicit NrClientConnectionTestSocket(const SOCKET socket) noexcept
                : socket_(socket)
            {
            }

            NrClientConnectionTestSocket(const NrClientConnectionTestSocket&) = delete;
            NrClientConnectionTestSocket& operator=(const NrClientConnectionTestSocket&) = delete;

            ~NrClientConnectionTestSocket() noexcept
            {
                if (socket_ != INVALID_SOCKET)
                {
                    (void)closesocket(socket_);
                }
            }

            [[nodiscard]] SOCKET Get() const noexcept
            {
                return socket_;
            }

        private:
            SOCKET socket_ = INVALID_SOCKET;
        };

        TEST(NrClientConnectionTests, ZeroAttemptGenerationCannotCreateConnection)
        {
            NrSocketAddressWin32 remoteAddress;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateClientConnectionMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);

            psnr::core::NrResult<std::unique_ptr<NrClientConnection>> connectionResult =
                NrClientConnection::Create(0, remoteAddress, *memoryPoolManager);

            EXPECT_EQ(connectionResult.ErrorCode(), psnr::core::NrErrorCode::InvalidArgument);
        }

        TEST(NrClientConnectionTests, BootstrapFailureClosesOwnedSocket)
        {
            NrClientConnectionTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrSocketAddressWin32 remoteAddress;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateClientConnectionMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            psnr::core::NrResult<std::unique_ptr<NrClientConnection>> connectionResult =
                NrClientConnection::Create(3, remoteAddress, *memoryPoolManager);
            ASSERT_TRUE(connectionResult.Succeeded());
            std::unique_ptr<NrClientConnection> connection = connectionResult.TakeValue();
            NrIocpPort closedIocpPort;

            EXPECT_EQ(connection->StartConnect(closedIocpPort).ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(connection->SocketState(), NrWin32SocketState::Closed);
        }

        TEST(NrClientConnectionTests, MissingSendContextFailsCreationAndReturnsAcquiredResources)
        {
            NrSocketAddressWin32 remoteAddress;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateClientConnectionMemoryPool(1);
            ASSERT_NE(memoryPoolManager, nullptr);

            psnr::core::NrResult<std::unique_ptr<NrClientConnection>> connectionResult =
                NrClientConnection::Create(4, remoteAddress, *memoryPoolManager);

            EXPECT_EQ(connectionResult.ErrorCode(), psnr::core::NrErrorCode::PoolExhausted);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::RecvBuffer).inUse,
                      0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::OverlappedContext).inUse,
                      0u);
        }

        TEST(NrClientConnectionTests, OwnsConnectReceiveAndSendStorageThroughIocpCompletions)
        {
            NrClientConnectionTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            ASSERT_TRUE(listenSocket.OpenOverlappedTcpIPv4().Succeeded());
            const NrEndpoint listenEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), 0};
            ASSERT_TRUE(listenSocket.Bind(listenEndpoint).Succeeded());
            ASSERT_TRUE(listenSocket.Listen(1).Succeeded());

            NrSocketAddressWin32 remoteAddress;
            int remoteAddressLength = remoteAddress.length;
            ASSERT_NE(getsockname(listenSocket.NativeSocket(), reinterpret_cast<sockaddr*>(&remoteAddress.address),
                                  &remoteAddressLength),
                      SOCKET_ERROR);
            remoteAddress.length = remoteAddressLength;

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());

            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateClientConnectionMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);

            psnr::core::NrResult<std::unique_ptr<NrClientConnection>> connectionResult =
                NrClientConnection::Create(5, remoteAddress, *memoryPoolManager);
            ASSERT_TRUE(connectionResult.Succeeded());
            std::unique_ptr<NrClientConnection> connection = connectionResult.TakeValue();

            const psnr::core::NrMemoryPoolStats recvBufferStats =
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::RecvBuffer);
            const psnr::core::NrMemoryPoolStats contextStats =
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::OverlappedContext);
            EXPECT_EQ(recvBufferStats.inUse, 1u);
            EXPECT_EQ(contextStats.inUse, 2u);
            EXPECT_TRUE(connection->SendContext().payloadRef.IsEmpty());
            EXPECT_EQ(NrClientIoContextOperations::AttemptGeneration(connection->SendContext()), 5u);

            ASSERT_TRUE(connection->StartConnect(iocpPort).Succeeded());
            EXPECT_EQ(connection->AttemptGeneration(), 5u);
            EXPECT_EQ(connection->SocketState(), NrWin32SocketState::ConnectPendingTcpIPv4);

            NrIocpCompletionPacket packet;
            ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());
            ASSERT_TRUE(packet.ioStatus.Succeeded());
            EXPECT_EQ(packet.completionKey, 0u);
            EXPECT_EQ(packet.overlapped, connection->ConnectContext().Overlapped());

            ASSERT_TRUE(connection->CompleteConnect().Succeeded());
            EXPECT_EQ(connection->SocketState(), NrWin32SocketState::ConnectedTcpIPv4);

            NrClientConnectionTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);

            ASSERT_TRUE(connection->PostRecv().Succeeded());
            EXPECT_TRUE(connection->HasPendingRecv());
            EXPECT_EQ(connection->PostRecv().ErrorCode(), psnr::core::NrErrorCode::InvalidState);

            const std::array<char, 4> serverBytes = {'r', 'e', 'c', 'v'};
            ASSERT_TRUE(SendAll(acceptedSocket.Get(), serverBytes.data(), static_cast<int>(serverBytes.size())));

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(recvPacket.ioStatus.Succeeded());
            EXPECT_EQ(recvPacket.overlapped, connection->RecvContext().header.Overlapped());
            EXPECT_EQ(recvPacket.bytesTransferred, serverBytes.size());

            ASSERT_TRUE(connection->CompleteRecv(connection->RecvContext(), recvPacket).Succeeded());
            EXPECT_FALSE(connection->HasPendingRecv());
            EXPECT_EQ(connection->ReadableRecvBytes(), serverBytes.size());

            const std::array<std::byte, 4> clientBytes = {
                std::byte{'s'},
                std::byte{'e'},
                std::byte{'n'},
                std::byte{'d'},
            };
            psnr::core::NrResult<psnr::core::NrPayloadRef> payloadResult =
                psnr::core::NrPayloadRefFactory::CreatePayloadRefFrom(*memoryPoolManager, std::span(clientBytes));
            ASSERT_TRUE(payloadResult.Succeeded());
            NrSendIoContext* const sendContext = &connection->SendContext();

            ASSERT_TRUE(connection->PostSend(payloadResult.TakeValue()).Succeeded());
            EXPECT_TRUE(connection->HasPendingSend());
            EXPECT_EQ(&connection->SendContext(), sendContext);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::OverlappedContext).inUse,
                      2u);

            NrIocpCompletionPacket sendPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(sendPacket).Succeeded());
            ASSERT_TRUE(sendPacket.ioStatus.Succeeded());
            EXPECT_EQ(sendPacket.overlapped, connection->SendContext().header.Overlapped());
            EXPECT_EQ(sendPacket.bytesTransferred, clientBytes.size());
            NrClientSendCompletionResult result = NrClientSendCompletionResult::None;
            ASSERT_TRUE(connection->CompleteSend(connection->SendContext(), sendPacket, result).Succeeded());
            EXPECT_EQ(result, NrClientSendCompletionResult::Completed);
            EXPECT_FALSE(connection->HasPendingSend());
            EXPECT_TRUE(connection->SendContext().payloadRef.IsEmpty());

            std::array<char, clientBytes.size()> receivedClientBytes{};
            ASSERT_TRUE(ReceiveAll(acceptedSocket.Get(), receivedClientBytes.data(),
                                   static_cast<int>(receivedClientBytes.size())));
            for (std::size_t index = 0; index < clientBytes.size(); ++index)
            {
                EXPECT_EQ(static_cast<std::byte>(receivedClientBytes[index]), clientBytes[index]);
            }

            ASSERT_TRUE(connection->Close().Succeeded());
            EXPECT_EQ(connection->SocketState(), NrWin32SocketState::Closed);
        }
    } // namespace
} // namespace psnr::runtime::internal
