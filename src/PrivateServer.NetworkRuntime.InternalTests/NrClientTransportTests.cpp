#include "pch.h"

#include "NrClientCommandChannel.h"
#include "NrClientConnectIoContext.h"
#include "NrClientControlCompletion.h"
#include "NrClientIoCompletionDispatcher.h"
#include "NrClientLifecycleStateMachine.h"
#include "NrClientTransport.h"
#include "NrClientTransportEventSink.h"
#include "NrEndpoint.h"
#include "NrErrorCode.h"
#include "NrIocpCompletionPacket.h"
#include "NrIocpPort.h"
#include "NrMemoryPoolTestUtils.h"
#include "NrPacketHeader.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrSocketAddressWin32.h"
#include "NrWin32Socket.h"
#include "NrWindows.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace psnr::runtime::internal
{
    namespace
    {
        constexpr std::size_t ClientPendingSendQueueCapacity = 4;

        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateClientTransportMemoryPool()
        {
            psnr::core::NrMemoryPoolManagerConfig config = psnr::core::test::MakeDefaultMemoryPoolManagerConfig();
            const psnr::core::NrResult<std::size_t> queueStorageBytesResult =
                NrClientPendingSendQueue::RequiredStorageBytes(ClientPendingSendQueueCapacity);
            EXPECT_TRUE(queueStorageBytesResult.Succeeded());
            if (queueStorageBytesResult.Failed())
            {
                return nullptr;
            }

            EXPECT_TRUE(
                psnr::core::test::SetPoolConfig(config, psnr::core::NrMemoryPoolRole::RecvBuffer,
                                                psnr::core::test::MakePoolConfig(psnr::core::NrMaxPacketLength)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::OverlappedContext,
                psnr::core::test::MakePoolConfig(std::max(sizeof(NrRecvIoContext), sizeof(NrSendIoContext)), 2)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::Payload64,
                psnr::core::test::MakePoolConfig(64, ClientPendingSendQueueCapacity)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::PayloadRefControl,
                psnr::core::test::MakePoolConfig(64, ClientPendingSendQueueCapacity)));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage,
                psnr::core::test::MakePoolConfig(queueStorageBytesResult.Value(), 1,
                                                 psnr::core::NrCacheLineSize)));
            return psnr::core::test::CreateMemoryPoolManager(config);
        }

        [[nodiscard]] std::unique_ptr<NrClientPendingSendQueue> CreateClientPendingSendQueue(
            psnr::core::NrMemoryPoolManager& memoryPoolManager)
        {
            psnr::core::NrResult<std::unique_ptr<NrClientPendingSendQueue>> queueResult =
                NrClientPendingSendQueue::Create(memoryPoolManager,
                                                 psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage,
                                                 ClientPendingSendQueueCapacity);
            EXPECT_TRUE(queueResult.Succeeded());
            return queueResult.Failed() ? nullptr : queueResult.TakeValue();
        }

        class NrClientTransportTestSocket final
        {
        public:
            explicit NrClientTransportTestSocket(const SOCKET socket) noexcept
                : socket_(socket)
            {
            }

            NrClientTransportTestSocket(const NrClientTransportTestSocket&) = delete;
            NrClientTransportTestSocket& operator=(const NrClientTransportTestSocket&) = delete;

            ~NrClientTransportTestSocket() noexcept
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

        class NrClientTransportTestWinsockRuntime final
        {
        public:
            NrClientTransportTestWinsockRuntime() noexcept
            {
                WSADATA data{};
                startResult_ = WSAStartup(MAKEWORD(2, 2), &data);
            }

            NrClientTransportTestWinsockRuntime(const NrClientTransportTestWinsockRuntime&) = delete;
            NrClientTransportTestWinsockRuntime& operator=(const NrClientTransportTestWinsockRuntime&) = delete;

            ~NrClientTransportTestWinsockRuntime() noexcept
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

        class NrRecordingClientTransportSink final : public INrClientTransportEventSink
        {
        public:
            [[nodiscard]] psnr::core::NrStatus PublishTransportConnected() noexcept override
            {
                ++connectedCount;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus PublishTransportConnectionFailed(
                const psnr::core::NrStatus transportStatus) noexcept override
            {
                ++connectionFailedCount;
                recordedStatus = transportStatus;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus PublishPacketReceived(
                const psnr::core::NrPacketType packetType, const std::span<const std::byte> payload) noexcept override
            {
                if (packetPublicationStatus.Failed() && packetTypes.size() >= successfulPacketPublicationsBeforeFailure)
                {
                    return packetPublicationStatus;
                }

                packetTypes.push_back(packetType);
                packetPayloads.emplace_back(payload.begin(), payload.end());
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus PublishTransportDisconnected(
                const NrClientDisconnectReason reason, const psnr::core::NrStatus transportStatus) noexcept override
            {
                ++disconnectedCount;
                disconnectReason = reason;
                disconnectedStatus = transportStatus;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus HandleEventSpaceAvailable() noexcept override
            {
                ++eventSpaceAvailableCount;
                return psnr::core::NrStatus::Success();
            }

            std::size_t connectedCount = 0;
            std::size_t connectionFailedCount = 0;
            std::size_t disconnectedCount = 0;
            std::size_t eventSpaceAvailableCount = 0;
            psnr::core::NrStatus recordedStatus;
            psnr::core::NrStatus packetPublicationStatus;
            psnr::core::NrStatus disconnectedStatus;
            std::size_t successfulPacketPublicationsBeforeFailure = 0;
            NrClientDisconnectReason disconnectReason = NrClientDisconnectReason::None;
            std::vector<psnr::core::NrPacketType> packetTypes;
            std::vector<std::vector<std::byte>> packetPayloads;
        };

        [[nodiscard]] std::vector<std::byte> MakePacketBytes(const std::uint16_t packetType,
                                                             const std::span<const std::byte> payload = {})
        {
            const std::uint16_t packetLength =
                static_cast<std::uint16_t>(psnr::core::NrPacketHeaderLength + payload.size());
            std::vector<std::byte> bytes;
            bytes.reserve(packetLength);
            bytes.push_back(static_cast<std::byte>(packetLength & 0xFF));
            bytes.push_back(static_cast<std::byte>((packetLength >> 8) & 0xFF));
            bytes.push_back(static_cast<std::byte>(packetType & 0xFF));
            bytes.push_back(static_cast<std::byte>((packetType >> 8) & 0xFF));
            bytes.push_back(static_cast<std::byte>(psnr::core::NrCurrentProtocolVersion));
            bytes.push_back(std::byte{0});
            bytes.insert(bytes.end(), payload.begin(), payload.end());
            return bytes;
        }

        [[nodiscard]] NrEndpoint ListenOnLoopback(NrWin32Socket& listenSocket) noexcept
        {
            const NrEndpoint listenEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), 0};
            if (listenSocket.OpenOverlappedTcpIPv4().Failed() || listenSocket.Bind(listenEndpoint).Failed() ||
                listenSocket.Listen(1).Failed())
            {
                return NrEndpoint{};
            }

            sockaddr_in boundAddress{};
            int boundAddressLength = sizeof(boundAddress);
            if (getsockname(listenSocket.NativeSocket(), reinterpret_cast<sockaddr*>(&boundAddress),
                            &boundAddressLength) == SOCKET_ERROR)
            {
                return NrEndpoint{};
            }

            return NrEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), ntohs(boundAddress.sin_port)};
        }

        [[nodiscard]] bool StartAndCompleteClientConnect(std::uint64_t& outGeneration, const NrEndpoint& endpoint,
                                                         NrClientTransport& transport,
                                                         NrClientCommandChannel& commandChannel,
                                                         NrIocpPort& iocpPort,
                                                         NrClientIoCompletionDispatcher& dispatcher) noexcept
        {
            if (commandChannel.SubmitConnect(endpoint, outGeneration).Failed())
            {
                return false;
            }

            NrIocpCompletionPacket commandPacket;
            if (iocpPort.WaitForCompletion(commandPacket).Failed() || commandPacket.overlapped != nullptr ||
                dispatcher.HandleControlCompletion(commandPacket).Failed())
            {
                return false;
            }

            NrIocpCompletionPacket packet;
            if (iocpPort.WaitForCompletion(packet).Failed() || dispatcher.HandleIoCompletion(packet).Failed())
            {
                return false;
            }

            return commandChannel.State() == NrClientLifecycleState::TransportConnected;
        }

        [[nodiscard]] psnr::core::NrStatus DispatchNextClientCompletion(
            NrIocpPort& iocpPort, NrClientIoCompletionDispatcher& dispatcher) noexcept
        {
            NrIocpCompletionPacket packet;
            const psnr::core::NrStatus waitStatus = iocpPort.WaitForCompletion(packet);
            if (waitStatus.Failed())
            {
                return waitStatus;
            }

            return packet.overlapped == nullptr ? dispatcher.HandleControlCompletion(packet)
                                                : dispatcher.HandleIoCompletion(packet);
        }

        [[nodiscard]] psnr::core::NrStatus DrainClientCommandsDirectly(
            NrClientTransport& transport, NrClientCommandChannel& commandChannel) noexcept
        {
            if (!commandChannel.TryBeginDrain())
            {
                return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
            }

            const psnr::core::NrResult<psnr::core::NrActorDrainReport> drainResult =
                transport.Drain(psnr::core::NrActorDrainBudget{16});
            const bool shouldReschedule = drainResult.Succeeded() && drainResult.Value().needsReschedule;
            const psnr::core::NrStatus completeStatus = commandChannel.CompleteDrain(shouldReschedule);
            return drainResult.Failed() ? drainResult.Status() : completeStatus;
        }

        [[nodiscard]] bool DispatchUntilWorkerStop(NrIocpPort& iocpPort,
                                                   NrClientIoCompletionDispatcher& dispatcher,
                                                   const std::size_t maxCompletionCount) noexcept
        {
            for (std::size_t completionIndex = 0; completionIndex < maxCompletionCount; ++completionIndex)
            {
                NrIocpCompletionPacket packet;
                if (iocpPort.WaitForCompletion(packet).Failed())
                {
                    return false;
                }

                if (packet.overlapped != nullptr)
                {
                    if (dispatcher.HandleIoCompletion(packet).Failed())
                    {
                        return false;
                    }
                    continue;
                }

                NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
                if (DecodeClientControlCompletion(packet, kind).Failed())
                {
                    return false;
                }

                if (kind == NrClientControlCompletionKind::Stop)
                {
                    return true;
                }

                if (dispatcher.HandleControlCompletion(packet).Failed())
                {
                    return false;
                }
            }

            return false;
        }

        TEST(NrClientTransportTests, BootstrapFailurePublishesOneTerminalOutcomeAndReturnsLifecycleToIdle)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrIocpPort closedIocpPort;
            ASSERT_TRUE(closedIocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(closedIocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(closedIocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            const NrEndpoint endpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), 1};
            ASSERT_TRUE(commandChannel.SubmitConnect(endpoint, generation).Succeeded());
            psnr::core::INrActor& actor = transport;
            const psnr::core::NrResult<psnr::core::NrActorDrainReport> invalidDrainResult =
                actor.Drain(psnr::core::NrActorDrainBudget{0});
            ASSERT_TRUE(invalidDrainResult.Failed());
            EXPECT_EQ(invalidDrainResult.Status().ErrorCode(), psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);

            NrIocpCompletionPacket commandPacket;
            ASSERT_TRUE(closedIocpPort.WaitForCompletion(commandPacket).Succeeded());
            ASSERT_TRUE(closedIocpPort.Close().Succeeded());
            ASSERT_TRUE(dispatcher.HandleControlCompletion(commandPacket).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(commandChannel.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(sink.connectionFailedCount, 1u);
            EXPECT_EQ(sink.recordedStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        }

        TEST(NrClientTransportTests, ShutdownWithoutConnectionPostsWorkerStop)
        {
            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(commandChannel.SubmitShutdown().Succeeded());
            ASSERT_TRUE(DispatchUntilWorkerStop(iocpPort, dispatcher, 2));

            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Shutdown);
            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_EQ(sink.connectedCount, 0u);
            EXPECT_EQ(sink.connectionFailedCount, 0u);
            EXPECT_EQ(sink.disconnectedCount, 0u);
        }

        TEST(NrClientTransportTests, DisconnectWhileConnectingDrainsConnectBeforePublishingLocalDisconnect)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            std::uint64_t generation = 0;
            ASSERT_TRUE(commandChannel.SubmitConnect(serverEndpoint, generation).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            ASSERT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnecting);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportDisconnecting);

            for (std::size_t completionIndex = 0;
                 completionIndex < 3 && lifecycle.State() != NrClientLifecycleState::Idle; ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }

            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(commandChannel.State(), NrClientLifecycleState::Idle);
            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_EQ(sink.connectedCount, 0u);
            EXPECT_EQ(sink.disconnectedCount, 1u);
            EXPECT_EQ(sink.disconnectReason, NrClientDisconnectReason::LocalRequested);
        }

        TEST(NrClientTransportTests, ShutdownWhileConnectingDrainsConnectBeforePostingWorkerStop)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(commandChannel.SubmitConnect(serverEndpoint, generation).Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());
            ASSERT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnecting);
            EXPECT_EQ(transport.PendingConnectIoCount(), 1u);

            ASSERT_TRUE(commandChannel.SubmitShutdown().Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnecting);

            ASSERT_TRUE(DispatchUntilWorkerStop(iocpPort, dispatcher, 6));
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Shutdown);
            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_EQ(transport.PendingConnectIoCount(), 0u);
            EXPECT_EQ(sink.connectedCount, 0u);
            EXPECT_EQ(sink.connectionFailedCount, 0u);
            EXPECT_EQ(sink.disconnectedCount, 0u);
        }

        TEST(NrClientTransportTests, DisconnectThenShutdownWhileConnectingDrainsConnectBeforePostingWorkerStop)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(commandChannel.SubmitConnect(serverEndpoint, generation).Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());
            ASSERT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnecting);
            ASSERT_EQ(transport.PendingConnectIoCount(), 1u);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());
            ASSERT_EQ(lifecycle.State(), NrClientLifecycleState::TransportDisconnecting);
            ASSERT_EQ(transport.PendingConnectIoCount(), 1u);

            ASSERT_TRUE(commandChannel.SubmitShutdown().Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportDisconnecting);
            EXPECT_EQ(commandChannel.State(), NrClientLifecycleState::Shutdown);
            EXPECT_EQ(transport.PendingConnectIoCount(), 1u);

            ASSERT_TRUE(DispatchUntilWorkerStop(iocpPort, dispatcher, 6));
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Shutdown);
            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_EQ(transport.PendingConnectIoCount(), 0u);
            EXPECT_EQ(sink.connectedCount, 0u);
            EXPECT_EQ(sink.connectionFailedCount, 0u);
            EXPECT_EQ(sink.disconnectedCount, 0u);
        }

        TEST(NrClientTransportTests, ReceiveCompletionPublishesFragmentedAndCombinedPacketsThenRepostsReceive)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(commandChannel.SubmitConnect(serverEndpoint, generation).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());

            NrIocpCompletionPacket packet;
            ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(packet).Succeeded());

            EXPECT_TRUE(transport.HasActiveConnection());
            EXPECT_TRUE(transport.HasPendingRecv());
            EXPECT_EQ(transport.ConnectionSocketState(), NrWin32SocketState::ConnectedTcpIPv4);
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnected);
            EXPECT_EQ(lifecycle.NextPendingKind(), NrClientLifecycleNotificationKind::None);
            EXPECT_EQ(sink.connectedCount, 1u);

            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            const std::array<std::byte, 3> firstPayload = {
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
            };
            const std::vector<std::byte> firstPacket = MakePacketBytes(11, firstPayload);
            const int firstFragmentLength = 4;
            ASSERT_EQ(
                send(acceptedSocket.Get(), reinterpret_cast<const char*>(firstPacket.data()), firstFragmentLength, 0),
                firstFragmentLength);

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());
            EXPECT_TRUE(transport.HasPendingRecv());
            EXPECT_TRUE(sink.packetTypes.empty());

            const int secondFragmentLength = static_cast<int>(firstPacket.size()) - firstFragmentLength;
            ASSERT_EQ(send(acceptedSocket.Get(),
                           reinterpret_cast<const char*>(firstPacket.data() + firstFragmentLength),
                           secondFragmentLength, 0),
                      secondFragmentLength);

            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(transport.HasPendingRecv());
            ASSERT_EQ(sink.packetTypes.size(), 1u);
            EXPECT_EQ(sink.packetTypes[0].value, 11u);
            EXPECT_EQ(sink.packetPayloads[0], std::vector<std::byte>(firstPayload.begin(), firstPayload.end()));

            const std::array<std::byte, 1> secondPayload = {std::byte{0x40}};
            const std::array<std::byte, 2> thirdPayload = {std::byte{0x50}, std::byte{0x60}};
            const std::vector<std::byte> secondPacket = MakePacketBytes(12, secondPayload);
            const std::vector<std::byte> thirdPacket = MakePacketBytes(13, thirdPayload);
            std::vector<std::byte> combinedPackets;
            combinedPackets.reserve(secondPacket.size() + thirdPacket.size());
            combinedPackets.insert(combinedPackets.end(), secondPacket.begin(), secondPacket.end());
            combinedPackets.insert(combinedPackets.end(), thirdPacket.begin(), thirdPacket.end());

            ASSERT_EQ(send(acceptedSocket.Get(), reinterpret_cast<const char*>(combinedPackets.data()),
                           static_cast<int>(combinedPackets.size()), 0),
                      combinedPackets.size());

            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(transport.HasPendingRecv());
            ASSERT_EQ(sink.packetTypes.size(), 3u);
            EXPECT_EQ(sink.packetTypes[1].value, 12u);
            EXPECT_EQ(sink.packetTypes[2].value, 13u);
            EXPECT_EQ(sink.packetPayloads[1], std::vector<std::byte>(secondPayload.begin(), secondPayload.end()));
            EXPECT_EQ(sink.packetPayloads[2], std::vector<std::byte>(thirdPayload.begin(), thirdPayload.end()));
        }

        TEST(NrClientTransportTests, CompletionFailureClosesConnectionAndPublishesFailure)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(commandChannel.SubmitConnect(serverEndpoint, generation).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());

            NrIocpCompletionPacket packet;
            ASSERT_TRUE(iocpPort.WaitForCompletion(packet).Succeeded());
            packet.ioStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 10061);
            ASSERT_TRUE(dispatcher.HandleIoCompletion(packet).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(commandChannel.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(sink.connectionFailedCount, 1u);
            EXPECT_EQ(sink.recordedStatus.ErrorCode(), psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(sink.recordedStatus.NativeErrorCode(), 10061u);
        }

        TEST(NrClientTransportTests, FullSendCompletionReleasesPayloadAndKeepsReceiveActive)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            const std::array<std::byte, 4> payloadBytes = {
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
                std::byte{0x40},
            };
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{70}, payloadBytes).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            ASSERT_TRUE(transport.HasPendingSend());

            for (std::size_t completionIndex = 0;
                 completionIndex < payloadBytes.size() + psnr::core::NrPacketHeaderLength &&
                 transport.HasPendingSend(); ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }

            EXPECT_TRUE(transport.HasActiveConnection());
            EXPECT_TRUE(transport.HasPendingRecv());
            EXPECT_FALSE(transport.HasPendingSend());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnected);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      0u);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            NrIocpCompletionPacket canceledRecvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(canceledRecvPacket).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(canceledRecvPacket).Succeeded());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
        }

        TEST(NrClientTransportTests, SubmitSendFramesCallerBytesAndDrainsInFifoOrder)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);

            std::array<std::byte, 2> firstBytes = {std::byte{'a'}, std::byte{'b'}};
            std::array<std::byte, 2> secondBytes = {std::byte{'c'}, std::byte{'d'}};
            std::vector<std::byte> expectedBytes = MakePacketBytes(71, firstBytes);
            const std::vector<std::byte> secondFrame = MakePacketBytes(72, secondBytes);
            expectedBytes.insert(expectedBytes.end(), secondFrame.begin(), secondFrame.end());

            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{71}, firstBytes).Succeeded());
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{72}, secondBytes).Succeeded());
            firstBytes.fill(std::byte{0xFF});
            secondBytes.fill(std::byte{0xEE});
            ASSERT_EQ(transport.PendingSendQueueDepth(), 2u);
            ASSERT_EQ(transport.PendingSendQueueHighWatermark(), 2u);

            for (std::size_t completionIndex = 0;
                 completionIndex < expectedBytes.size() + 2 &&
                 (transport.PendingSendQueueDepth() != 0 || transport.HasPendingSend()); ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }
            ASSERT_FALSE(transport.HasPendingSend());
            ASSERT_EQ(transport.PendingSendQueueDepth(), 0u);

            std::vector<std::byte> receivedBytes(expectedBytes.size());
            ASSERT_EQ(recv(acceptedSocket.Get(), reinterpret_cast<char*>(receivedBytes.data()),
                           static_cast<int>(receivedBytes.size()),
                           MSG_WAITALL),
                      static_cast<int>(receivedBytes.size()));
            EXPECT_EQ(receivedBytes, expectedBytes);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      0u);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            for (std::size_t completionIndex = 0;
                 completionIndex < 3 && lifecycle.State() != NrClientLifecycleState::Idle; ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
        }

        TEST(NrClientTransportTests, SubmitSendBoundsActiveAndQueuedPayloadsTogether)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            const std::array<std::byte, 1> semanticPayload = {std::byte{0x44}};

            for (std::size_t index = 0; index < ClientPendingSendQueueCapacity; ++index)
            {
                ASSERT_TRUE(commandChannel.SubmitSend(
                    psnr::core::NrPacketType{static_cast<std::uint16_t>(80 + index)}, semanticPayload).Succeeded());
            }
            ASSERT_EQ(transport.PendingSendQueueDepth(), ClientPendingSendQueueCapacity);
            ASSERT_EQ(transport.PendingSendQueueHighWatermark(), ClientPendingSendQueueCapacity);

            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            ASSERT_TRUE(transport.HasPendingSend());
            ASSERT_EQ(transport.PendingSendQueueDepth(), ClientPendingSendQueueCapacity - 1);
            EXPECT_EQ(commandChannel.SubmitSend(psnr::core::NrPacketType{99}, semanticPayload).ErrorCode(),
                      psnr::core::NrErrorCode::QueueFull);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      ClientPendingSendQueueCapacity);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            for (std::size_t completionIndex = 0;
                 completionIndex < ClientPendingSendQueueCapacity + 2 &&
                 lifecycle.State() != NrClientLifecycleState::Idle; ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }

            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(transport.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      0u);
        }

        TEST(NrClientTransportTests, DisconnectAdmissionClosesSendBeforeActorDrain)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);

            const std::array<std::byte, 2> activeBytes = {std::byte{'a'}, std::byte{'b'}};
            const std::array<std::byte, 2> queuedBytes = {std::byte{'c'}, std::byte{'d'}};
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{73}, activeBytes).Succeeded());
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{74}, queuedBytes).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            ASSERT_TRUE(transport.HasPendingSend());
            ASSERT_EQ(transport.PendingSendQueueDepth(), 1u);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            EXPECT_EQ(commandChannel.SubmitSend(psnr::core::NrPacketType{75}, queuedBytes).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnected);
            EXPECT_EQ(transport.PendingSendQueueDepth(), 1u);

            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());

            for (std::size_t completionIndex = 0;
                 completionIndex < 5 && lifecycle.State() != NrClientLifecycleState::Idle; ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(transport.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      0u);
        }

        TEST(NrClientTransportTests, ZeroByteReceivePublishesRemoteClosedAndStopsReceive)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            ASSERT_NE(shutdown(acceptedSocket.Get(), SD_SEND), SOCKET_ERROR);

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_EQ(recvPacket.bytesTransferred, 0u);
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_FALSE(transport.HasPendingRecv());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(sink.disconnectedCount, 1u);
            EXPECT_EQ(sink.disconnectReason, NrClientDisconnectReason::RemoteClosed);
            EXPECT_TRUE(sink.disconnectedStatus.Succeeded());
        }

        TEST(NrClientTransportTests, LocalDisconnectRetainsResourcesUntilPendingReceiveAndSendDrain)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            ASSERT_TRUE(transport.HasPendingRecv());
            const std::array<std::byte, 4> payloadBytes = {
                std::byte{0x50},
                std::byte{0x60},
                std::byte{0x70},
                std::byte{0x80},
            };
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{75}, payloadBytes).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            ASSERT_TRUE(transport.HasPendingSend());
            const std::array<std::byte, 2> queuedPayloadBytes = {
                std::byte{0x90},
                std::byte{0xA0},
            };
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{76}, queuedPayloadBytes).Succeeded());
            ASSERT_EQ(transport.PendingSendQueueDepth(), 1u);
            ASSERT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      2u);

            ASSERT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            EXPECT_TRUE(commandChannel.SubmitDisconnect().Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_TRUE(transport.HasPendingRecv());
            EXPECT_TRUE(transport.HasPendingSend());
            EXPECT_EQ(transport.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(transport.ConnectionSocketState(), NrWin32SocketState::Closed);
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportDisconnecting);
            EXPECT_EQ(sink.disconnectedCount, 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::RecvBuffer).inUse, 1u);
            EXPECT_EQ(
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::OverlappedContext).inUse, 2u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      1u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      1u);

            for (std::size_t completionIndex = 0;
                 completionIndex < 3 && lifecycle.State() != NrClientLifecycleState::Idle; ++completionIndex)
            {
                ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            }

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_FALSE(transport.HasPendingRecv());
            EXPECT_FALSE(transport.HasPendingSend());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(sink.disconnectedCount, 1u);
            EXPECT_EQ(sink.disconnectReason, NrClientDisconnectReason::LocalRequested);
            EXPECT_TRUE(sink.disconnectedStatus.Succeeded());
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::RecvBuffer).inUse, 0u);
            EXPECT_EQ(
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::OverlappedContext).inUse, 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      0u);
        }

        TEST(NrClientTransportTests, ShutdownDrainsPendingReceiveAndSendBeforePostingWorkerStop)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);

            const std::array<std::byte, 4> payloadBytes = {
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
                std::byte{0x40},
            };
            ASSERT_TRUE(commandChannel.SubmitSend(psnr::core::NrPacketType{77}, payloadBytes).Succeeded());
            ASSERT_TRUE(DispatchNextClientCompletion(iocpPort, dispatcher).Succeeded());
            ASSERT_TRUE(transport.HasPendingRecv());
            ASSERT_TRUE(transport.HasPendingSend());
            EXPECT_EQ(transport.PendingRecvIoCount(), 1u);
            EXPECT_EQ(transport.PendingSendIoCount(), 1u);

            ASSERT_TRUE(commandChannel.SubmitShutdown().Succeeded());
            ASSERT_TRUE(DrainClientCommandsDirectly(transport, commandChannel).Succeeded());

            EXPECT_TRUE(transport.HasPendingRecv());
            EXPECT_TRUE(transport.HasPendingSend());
            EXPECT_EQ(transport.ConnectionSocketState(), NrWin32SocketState::Closed);
            EXPECT_NE(lifecycle.State(), NrClientLifecycleState::Shutdown);
            EXPECT_EQ(sink.disconnectedCount, 0u);

            ASSERT_TRUE(DispatchUntilWorkerStop(iocpPort, dispatcher, 6));

            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Shutdown);
            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_FALSE(transport.HasPendingRecv());
            EXPECT_FALSE(transport.HasPendingSend());
            EXPECT_EQ(transport.PendingRecvIoCount(), 0u);
            EXPECT_EQ(transport.PendingSendIoCount(), 0u);
            EXPECT_EQ(sink.disconnectedCount, 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::RecvBuffer).inUse, 0u);
            EXPECT_EQ(
                psnr::core::test::Stats(*memoryPoolManager, psnr::core::NrMemoryPoolRole::OverlappedContext).inUse, 0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::Payload64).inUse,
                      0u);
            EXPECT_EQ(psnr::core::test::Stats(*memoryPoolManager,
                                              psnr::core::NrMemoryPoolRole::PayloadRefControl).inUse,
                      0u);
        }

        TEST(NrClientTransportTests, MalformedReceivePublishesProtocolErrorAndStopsReceive)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            std::vector<std::byte> malformedPacket = MakePacketBytes(21);
            malformedPacket[0] = std::byte{0x05};
            ASSERT_EQ(send(acceptedSocket.Get(), reinterpret_cast<const char*>(malformedPacket.data()),
                           static_cast<int>(malformedPacket.size()), 0),
                      malformedPacket.size());

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_FALSE(transport.HasPendingRecv());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_TRUE(sink.packetTypes.empty());
            EXPECT_EQ(sink.disconnectedCount, 1u);
            EXPECT_EQ(sink.disconnectReason, NrClientDisconnectReason::ProtocolError);
            EXPECT_EQ(sink.disconnectedStatus.ErrorCode(), psnr::core::NrErrorCode::ProtocolError);
        }

        TEST(NrClientTransportTests, PacketQueuePressurePublishesReceivePressureAndStopsReceive)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            sink.packetPublicationStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::QueueFull);
            sink.successfulPacketPublicationsBeforeFailure = 1;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            const std::vector<std::byte> committedPacket = MakePacketBytes(22);
            const std::vector<std::byte> rejectedPacket = MakePacketBytes(23);
            std::vector<std::byte> combinedPackets;
            combinedPackets.reserve(committedPacket.size() + rejectedPacket.size());
            combinedPackets.insert(combinedPackets.end(), committedPacket.begin(), committedPacket.end());
            combinedPackets.insert(combinedPackets.end(), rejectedPacket.begin(), rejectedPacket.end());
            ASSERT_EQ(send(acceptedSocket.Get(), reinterpret_cast<const char*>(combinedPackets.data()),
                           static_cast<int>(combinedPackets.size()), 0),
                      combinedPackets.size());

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_FALSE(transport.HasPendingRecv());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            ASSERT_EQ(sink.packetTypes.size(), 1u);
            EXPECT_EQ(sink.packetTypes[0].value, 22u);
            EXPECT_EQ(sink.disconnectedCount, 1u);
            EXPECT_EQ(sink.disconnectReason, NrClientDisconnectReason::ReceivePressure);
            EXPECT_EQ(sink.disconnectedStatus.ErrorCode(), psnr::core::NrErrorCode::QueueFull);
        }

        TEST(NrClientTransportTests, FailedReceivePublishesTransportErrorAndStopsReceive)
        {
            NrClientTransportTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrWin32Socket listenSocket;
            const NrEndpoint serverEndpoint = ListenOnLoopback(listenSocket);
            ASSERT_NE(serverEndpoint.port, 0);

            NrIocpPort iocpPort;
            ASSERT_TRUE(iocpPort.Create().Succeeded());
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            ASSERT_TRUE(StartAndCompleteClientConnect(generation, serverEndpoint, transport, commandChannel, iocpPort,
                                                      dispatcher));
            NrClientTransportTestSocket acceptedSocket(accept(listenSocket.NativeSocket(), nullptr, nullptr));
            ASSERT_NE(acceptedSocket.Get(), INVALID_SOCKET);
            const std::array<char, 1> serverBytes = {'x'};
            ASSERT_EQ(send(acceptedSocket.Get(), serverBytes.data(), static_cast<int>(serverBytes.size()), 0),
                      serverBytes.size());

            NrIocpCompletionPacket recvPacket;
            ASSERT_TRUE(iocpPort.WaitForCompletion(recvPacket).Succeeded());
            recvPacket.bytesTransferred = 0;
            recvPacket.ioStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 10054);
            ASSERT_TRUE(dispatcher.HandleIoCompletion(recvPacket).Succeeded());

            EXPECT_FALSE(transport.HasActiveConnection());
            EXPECT_FALSE(transport.HasPendingRecv());
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(sink.disconnectedCount, 1u);
            EXPECT_EQ(sink.disconnectReason, NrClientDisconnectReason::TransportError);
            EXPECT_EQ(sink.disconnectedStatus.ErrorCode(), psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(sink.disconnectedStatus.NativeErrorCode(), 10054u);
        }

        TEST(NrClientTransportTests, SameGenerationForeignConnectContextIsRejected)
        {
            NrIocpPort iocpPort;
            NrClientLifecycleStateMachine lifecycle;
            std::uint64_t generation = 0;
            ASSERT_TRUE(lifecycle.BeginConnect(generation).Succeeded());
            NrRecordingClientTransportSink sink;
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = CreateClientTransportMemoryPool();
            ASSERT_NE(memoryPoolManager, nullptr);
            std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue =
                CreateClientPendingSendQueue(*memoryPoolManager);
            ASSERT_NE(pendingSendQueue, nullptr);
            NrClientCommandChannel commandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue);
            NrClientTransport transport(iocpPort, *memoryPoolManager, commandChannel, lifecycle, sink);
            NrClientIoCompletionDispatcher dispatcher(transport);

            NrSocketAddressWin32 remoteAddress;
            NrClientConnectIoContext foreignContext(generation, remoteAddress);
            NrIocpCompletionPacket packet;
            packet.overlapped = foreignContext.Overlapped();

            EXPECT_EQ(dispatcher.HandleIoCompletion(packet).ErrorCode(), psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(lifecycle.State(), NrClientLifecycleState::TransportConnecting);
            EXPECT_EQ(sink.connectionFailedCount, 0u);
        }
    } // namespace
} // namespace psnr::runtime::internal
