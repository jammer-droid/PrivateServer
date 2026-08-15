#include "pch.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <PrivateServer/NetworkRuntime/NrClient.h>
#include <PrivateServer/NetworkRuntime/NrClientConfig.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    namespace
    {
        constexpr std::size_t PublicTestPacketHeaderLength = 6;
        constexpr std::uint8_t PublicTestProtocolVersion = 1;
        constexpr std::chrono::seconds PublicTestTimeout{5};

        class NrPublicTestWinsockRuntime final
        {
        public:
            NrPublicTestWinsockRuntime() noexcept
            {
                WSADATA data{};
                startResult_ = WSAStartup(MAKEWORD(2, 2), &data);
            }

            NrPublicTestWinsockRuntime(const NrPublicTestWinsockRuntime&) = delete;
            NrPublicTestWinsockRuntime& operator=(const NrPublicTestWinsockRuntime&) = delete;

            ~NrPublicTestWinsockRuntime() noexcept
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

        class NrPublicTestSocket final
        {
        public:
            NrPublicTestSocket() noexcept = default;

            explicit NrPublicTestSocket(const SOCKET socket) noexcept
                : socket_(socket)
            {
            }

            NrPublicTestSocket(const NrPublicTestSocket&) = delete;
            NrPublicTestSocket& operator=(const NrPublicTestSocket&) = delete;

            ~NrPublicTestSocket() noexcept
            {
                Close();
            }

            [[nodiscard]] SOCKET Get() const noexcept
            {
                return socket_;
            }

            [[nodiscard]] bool IsValid() const noexcept
            {
                return socket_ != INVALID_SOCKET;
            }

            void Reset(const SOCKET socket = INVALID_SOCKET) noexcept
            {
                Close();
                socket_ = socket;
            }

            [[nodiscard]] SOCKET Release() noexcept
            {
                return std::exchange(socket_, INVALID_SOCKET);
            }

            void Close() noexcept
            {
                if (socket_ != INVALID_SOCKET)
                {
                    static_cast<void>(closesocket(socket_));
                    socket_ = INVALID_SOCKET;
                }
            }

        private:
            SOCKET socket_ = INVALID_SOCKET;
        };

        [[nodiscard]] NrEndpoint CreatePublicLoopbackListener(NrPublicTestSocket& outListener) noexcept
        {
            NrPublicTestSocket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
            if (!listener.IsValid())
            {
                return NrEndpoint{};
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (bind(listener.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
                listen(listener.Get(), 1) == SOCKET_ERROR)
            {
                return NrEndpoint{};
            }

            int addressLength = sizeof(address);
            if (getsockname(listener.Get(), reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR)
            {
                return NrEndpoint{};
            }

            outListener.Reset(listener.Release());
            return NrEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Loopback(), ntohs(address.sin_port)};
        }

        [[nodiscard]] SOCKET AcceptPublicLoopbackClient(const SOCKET listener) noexcept
        {
            fd_set readableSockets;
            FD_ZERO(&readableSockets);
            FD_SET(listener, &readableSockets);

            timeval timeout{};
            timeout.tv_sec = static_cast<long>(PublicTestTimeout.count());
            if (select(0, &readableSockets, nullptr, nullptr, &timeout) != 1)
            {
                return INVALID_SOCKET;
            }

            const SOCKET accepted = accept(listener, nullptr, nullptr);
            if (accepted == INVALID_SOCKET)
            {
                return INVALID_SOCKET;
            }

            const DWORD socketTimeoutMilliseconds =
                static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(PublicTestTimeout).count());
            static_cast<void>(setsockopt(accepted, SOL_SOCKET, SO_RCVTIMEO,
                                        reinterpret_cast<const char*>(&socketTimeoutMilliseconds),
                                        sizeof(socketTimeoutMilliseconds)));
            static_cast<void>(setsockopt(accepted, SOL_SOCKET, SO_SNDTIMEO,
                                        reinterpret_cast<const char*>(&socketTimeoutMilliseconds),
                                        sizeof(socketTimeoutMilliseconds)));
            return accepted;
        }

        [[nodiscard]] bool SendAll(const SOCKET socket, const std::byte* bytes, const std::size_t length) noexcept
        {
            std::size_t sentLength = 0;
            while (sentLength < length)
            {
                const int remainingLength = static_cast<int>(length - sentLength);
                const int result = send(socket, reinterpret_cast<const char*>(bytes + sentLength), remainingLength, 0);
                if (result <= 0)
                {
                    return false;
                }
                sentLength += static_cast<std::size_t>(result);
            }
            return true;
        }

        [[nodiscard]] bool ReceiveExactly(const SOCKET socket, std::byte* bytes, const std::size_t length) noexcept
        {
            std::size_t receivedLength = 0;
            while (receivedLength < length)
            {
                const int remainingLength = static_cast<int>(length - receivedLength);
                const int result = recv(socket, reinterpret_cast<char*>(bytes + receivedLength), remainingLength, 0);
                if (result <= 0)
                {
                    return false;
                }
                receivedLength += static_cast<std::size_t>(result);
            }
            return true;
        }

        [[nodiscard]] std::vector<std::byte> MakePublicTestFrame(const std::uint16_t packetType,
                                                                 const std::span<const std::byte> payload)
        {
            const std::uint16_t packetLength =
                static_cast<std::uint16_t>(PublicTestPacketHeaderLength + payload.size());
            std::vector<std::byte> frame;
            frame.reserve(packetLength);
            frame.push_back(static_cast<std::byte>(packetLength & 0xFF));
            frame.push_back(static_cast<std::byte>((packetLength >> 8) & 0xFF));
            frame.push_back(static_cast<std::byte>(packetType & 0xFF));
            frame.push_back(static_cast<std::byte>((packetType >> 8) & 0xFF));
            frame.push_back(static_cast<std::byte>(PublicTestProtocolVersion));
            frame.push_back(std::byte{0});
            frame.insert(frame.end(), payload.begin(), payload.end());
            return frame;
        }

        [[nodiscard]] bool WaitForClientEvent(NrClient& client, const NrClientEventKind expectedKind,
                                              NrClientEvent& outEvent) noexcept
        {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicTestTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                NrClientEvent event;
                const NrStatus popStatus = client.TryPopEvent(&event);
                if (popStatus.Succeeded())
                {
                    if (event.Kind() != expectedKind)
                    {
                        return false;
                    }
                    outEvent = std::move(event);
                    return true;
                }

                if (popStatus.ErrorCode() != NrErrorCode::QueueEmpty)
                {
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }

        [[nodiscard]] bool WaitForClientState(const NrClient& client,
                                              const NrClientLifecycleState expectedState) noexcept
        {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicTestTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                NrClientSnapshot snapshot;
                if (client.CaptureSnapshot(&snapshot).Failed())
                {
                    return false;
                }

                if (snapshot.LifecycleState() == expectedState)
                {
                    return true;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }

        TEST(NrClientTests, DefaultConfigUsesBoundedClientCapacities)
        {
            const NrClientConfig config;

            EXPECT_EQ(NrDefaultClientEventQueueCapacity, 128u);
            EXPECT_EQ(NrDefaultClientPayloadQueueCapacity, 1024u);
            EXPECT_EQ(config.eventQueueCapacity, NrDefaultClientEventQueueCapacity);
            EXPECT_EQ(config.payloadQueueCapacity, NrDefaultClientPayloadQueueCapacity);
        }

        TEST(NrClientTests, ConfigIsTriviallyCopyablePublicValue)
        {
            EXPECT_TRUE(std::is_trivially_copyable_v<NrClientConfig>);
            EXPECT_TRUE(std::is_standard_layout_v<NrClientConfig>);

            NrClientConfig source;
            source.eventQueueCapacity = 7;
            source.payloadQueueCapacity = 11;

            const NrClientConfig copy = source;
            source.eventQueueCapacity = 13;
            source.payloadQueueCapacity = 17;

            EXPECT_EQ(copy.eventQueueCapacity, 7u);
            EXPECT_EQ(copy.payloadQueueCapacity, 11u);
        }

        TEST(NrClientTests, DefaultClientIsInvalidAndOperationsFail)
        {
            NrClient client;
            NrClientEvent event;
            NrClientSnapshot snapshot;

            EXPECT_FALSE(client.IsValid());
            EXPECT_EQ(client.Connect(NrEndpoint{}).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Disconnect().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Send(NrPacketType{}, NrByteView{}).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.TryPopEvent(&event).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.CaptureSnapshot(&snapshot).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Shutdown().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_FALSE(event.IsValid());
            EXPECT_FALSE(snapshot.IsValid());
        }

        TEST(NrClientTests, NullOutputsAreRejectedAtPublicBoundary)
        {
            NrClient client;

            EXPECT_EQ(NrClient::Create(NrClientConfig{}, nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(client.TryPopEvent(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(client.CaptureSnapshot(nullptr).ErrorCode(), NrErrorCode::InvalidArgument);
        }

        TEST(NrClientTests, CreateReturnsIdleClientWithEmptyBoundedStorage)
        {
            NrClient client;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());
            EXPECT_TRUE(client.IsValid());

            NrClientEvent event;
            EXPECT_EQ(client.TryPopEvent(&event).ErrorCode(), NrErrorCode::QueueEmpty);
            EXPECT_FALSE(event.IsValid());

            NrClientSnapshot snapshot;
            ASSERT_TRUE(client.CaptureSnapshot(&snapshot).Succeeded());
            EXPECT_TRUE(snapshot.IsValid());
            EXPECT_EQ(snapshot.LifecycleState(), NrClientLifecycleState::Idle);
            EXPECT_EQ(snapshot.PendingIoCount(), 0u);
            EXPECT_EQ(snapshot.EventQueueDepth(), 0u);
            EXPECT_EQ(snapshot.EventQueueHighWatermark(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueDepth(), 0u);
            EXPECT_EQ(snapshot.PendingSendQueueHighWatermark(), 0u);
        }

        TEST(NrClientTests, CreateRejectsCapacityOutsideMpscRequirementsAndPreservesOutput)
        {
            NrClient client;

            NrClientConfig config;
            config.eventQueueCapacity = 0;
            EXPECT_EQ(NrClient::Create(config, &client).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_FALSE(client.IsValid());

            config = NrClientConfig{};
            config.payloadQueueCapacity = 0;
            EXPECT_EQ(NrClient::Create(config, &client).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_FALSE(client.IsValid());

            config = NrClientConfig{};
            config.eventQueueCapacity = 1;
            EXPECT_EQ(NrClient::Create(config, &client).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_FALSE(client.IsValid());

            config = NrClientConfig{};
            config.payloadQueueCapacity = 3;
            EXPECT_EQ(NrClient::Create(config, &client).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_FALSE(client.IsValid());

            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());
            EXPECT_EQ(NrClient::Create(NrClientConfig{}, &client).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_TRUE(client.IsValid());
        }

        TEST(NrClientTests, MoveTransfersValidityAndMovedFromOperationsFail)
        {
            NrClient source;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &source).Succeeded());

            NrClient target(std::move(source));
            EXPECT_FALSE(source.IsValid());
            EXPECT_TRUE(target.IsValid());
            EXPECT_EQ(source.Shutdown().ErrorCode(), NrErrorCode::InvalidState);

            NrClient assigned;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &assigned).Succeeded());
            assigned = std::move(target);
            EXPECT_FALSE(target.IsValid());
            EXPECT_TRUE(assigned.IsValid());
        }

        TEST(NrClientTests, PublicOperationsValidateBeforeTransportAdmission)
        {
            NrClient client;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());

            const std::array<std::byte, 3> payload = {std::byte{1}, std::byte{2}, std::byte{3}};
            EXPECT_EQ(client.Connect(NrEndpoint{}).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(client.Disconnect().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Send(NrPacketType{9}, NrByteView{payload.data(), 3}).ErrorCode(),
                      NrErrorCode::InvalidState);
            EXPECT_EQ(client.Send(NrPacketType{9}, NrByteView{nullptr, 3}).ErrorCode(),
                      NrErrorCode::InvalidArgument);

            NrClientSnapshot snapshot;
            ASSERT_TRUE(client.CaptureSnapshot(&snapshot).Succeeded());
            EXPECT_EQ(snapshot.LifecycleState(), NrClientLifecycleState::Idle);
        }

        TEST(NrClientTests, ShutdownIsIdempotentAndSnapshotRemainsObservable)
        {
            NrClient client;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());

            EXPECT_TRUE(client.Shutdown().Succeeded());
            EXPECT_TRUE(client.Shutdown().Succeeded());

            NrClientSnapshot snapshot;
            ASSERT_TRUE(client.CaptureSnapshot(&snapshot).Succeeded());
            EXPECT_TRUE(snapshot.IsValid());
            EXPECT_EQ(snapshot.LifecycleState(), NrClientLifecycleState::Shutdown);
            EXPECT_EQ(snapshot.EventQueueDepth(), 0u);

            NrClientEvent event;
            EXPECT_EQ(client.TryPopEvent(&event).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Connect(NrEndpoint{}).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Disconnect().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(client.Send(NrPacketType{}, NrByteView{}).ErrorCode(), NrErrorCode::InvalidState);
        }

        TEST(NrClientTests, PublicLoopbackSendsAndReceivesFramesAndPoppedPayloadOutlivesShutdown)
        {
            NrPublicTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrPublicTestSocket listener;
            const NrEndpoint endpoint = CreatePublicLoopbackListener(listener);
            ASSERT_NE(endpoint.port, 0);

            NrClient client;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());
            ASSERT_TRUE(client.Connect(endpoint).Succeeded());

            NrPublicTestSocket peer(AcceptPublicLoopbackClient(listener.Get()));
            ASSERT_TRUE(peer.IsValid());

            NrClientEvent connectedEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::TransportConnected, connectedEvent));
            ASSERT_TRUE(WaitForClientState(client, NrClientLifecycleState::TransportConnected));

            NrClientSnapshot connectedSnapshot;
            ASSERT_TRUE(client.CaptureSnapshot(&connectedSnapshot).Succeeded());
            EXPECT_EQ(connectedSnapshot.PendingConnectIoCount(), 0u);
            EXPECT_EQ(connectedSnapshot.PendingRecvIoCount(), 1u);

            const std::array<std::byte, 4> clientPayload = {
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
                std::byte{0x40},
            };
            ASSERT_TRUE(client.Send(NrPacketType{201},
                                    NrByteView{clientPayload.data(),
                                               static_cast<std::uint32_t>(clientPayload.size())})
                            .Succeeded());

            const std::vector<std::byte> expectedClientFrame = MakePublicTestFrame(201, clientPayload);
            std::vector<std::byte> receivedClientFrame(expectedClientFrame.size());
            ASSERT_TRUE(ReceiveExactly(peer.Get(), receivedClientFrame.data(), receivedClientFrame.size()));
            EXPECT_TRUE(receivedClientFrame == expectedClientFrame);

            const std::array<std::byte, 3> firstServerPayload = {
                std::byte{0x51},
                std::byte{0x52},
                std::byte{0x53},
            };
            const std::array<std::byte, 2> secondServerPayload = {
                std::byte{0x61},
                std::byte{0x62},
            };
            const std::vector<std::byte> firstServerFrame = MakePublicTestFrame(301, firstServerPayload);
            const std::vector<std::byte> secondServerFrame = MakePublicTestFrame(302, secondServerPayload);

            constexpr std::size_t FragmentLength = 3;
            ASSERT_TRUE(SendAll(peer.Get(), firstServerFrame.data(), FragmentLength));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            std::vector<std::byte> combinedTail;
            combinedTail.insert(combinedTail.end(), firstServerFrame.begin() + FragmentLength, firstServerFrame.end());
            combinedTail.insert(combinedTail.end(), secondServerFrame.begin(), secondServerFrame.end());
            ASSERT_TRUE(SendAll(peer.Get(), combinedTail.data(), combinedTail.size()));

            NrClientEvent firstPacketEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::PacketReceived, firstPacketEvent));
            NrPacketType firstPacketType;
            NrByteView firstPayloadView;
            ASSERT_TRUE(firstPacketEvent.GetPacketType(&firstPacketType).Succeeded());
            ASSERT_TRUE(firstPacketEvent.GetPayload(&firstPayloadView).Succeeded());
            EXPECT_EQ(firstPacketType, NrPacketType{301});
            ASSERT_EQ(firstPayloadView.size, firstServerPayload.size());
            EXPECT_TRUE(std::equal(firstServerPayload.begin(), firstServerPayload.end(), firstPayloadView.data));

            NrClientEvent secondPacketEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::PacketReceived, secondPacketEvent));
            NrPacketType secondPacketType;
            NrByteView secondPayloadView;
            ASSERT_TRUE(secondPacketEvent.GetPacketType(&secondPacketType).Succeeded());
            ASSERT_TRUE(secondPacketEvent.GetPayload(&secondPayloadView).Succeeded());
            EXPECT_EQ(secondPacketType, NrPacketType{302});
            ASSERT_EQ(secondPayloadView.size, secondServerPayload.size());
            EXPECT_TRUE(std::equal(secondServerPayload.begin(), secondServerPayload.end(), secondPayloadView.data));

            ASSERT_TRUE(client.Disconnect().Succeeded());
            NrClientEvent disconnectedEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::TransportDisconnected, disconnectedEvent));
            NrClientDisconnectReason disconnectReason = NrClientDisconnectReason::None;
            ASSERT_TRUE(disconnectedEvent.GetDisconnectReason(&disconnectReason).Succeeded());
            EXPECT_EQ(disconnectReason, NrClientDisconnectReason::LocalRequested);
            ASSERT_TRUE(WaitForClientState(client, NrClientLifecycleState::Idle));

            NrClientSnapshot disconnectedSnapshot;
            ASSERT_TRUE(client.CaptureSnapshot(&disconnectedSnapshot).Succeeded());
            EXPECT_EQ(disconnectedSnapshot.PendingIoCount(), 0u);

            ASSERT_TRUE(client.Shutdown().Succeeded());

            NrByteView payloadAfterShutdown;
            ASSERT_TRUE(firstPacketEvent.GetPayload(&payloadAfterShutdown).Succeeded());
            ASSERT_EQ(payloadAfterShutdown.size, firstServerPayload.size());
            EXPECT_TRUE(std::equal(firstServerPayload.begin(), firstServerPayload.end(), payloadAfterShutdown.data));
        }

        TEST(NrClientTests, PublicLoopbackReportsRemoteClose)
        {
            NrPublicTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrPublicTestSocket listener;
            const NrEndpoint endpoint = CreatePublicLoopbackListener(listener);
            ASSERT_NE(endpoint.port, 0);

            NrClient client;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());
            ASSERT_TRUE(client.Connect(endpoint).Succeeded());

            NrPublicTestSocket peer(AcceptPublicLoopbackClient(listener.Get()));
            ASSERT_TRUE(peer.IsValid());

            NrClientEvent connectedEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::TransportConnected, connectedEvent));
            peer.Close();

            NrClientEvent disconnectedEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::TransportDisconnected, disconnectedEvent));
            NrClientDisconnectReason disconnectReason = NrClientDisconnectReason::None;
            ASSERT_TRUE(disconnectedEvent.GetDisconnectReason(&disconnectReason).Succeeded());
            EXPECT_EQ(disconnectReason, NrClientDisconnectReason::RemoteClosed);
            EXPECT_TRUE(client.Shutdown().Succeeded());
        }

        TEST(NrClientTests, PublicLoopbackReportsConnectFailureAsEvent)
        {
            NrPublicTestWinsockRuntime winsockRuntime;
            ASSERT_TRUE(winsockRuntime.Started());

            NrPublicTestSocket listener;
            const NrEndpoint closedEndpoint = CreatePublicLoopbackListener(listener);
            ASSERT_NE(closedEndpoint.port, 0);
            listener.Close();

            NrClient client;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &client).Succeeded());
            ASSERT_TRUE(client.Connect(closedEndpoint).Succeeded());

            NrClientEvent failedEvent;
            ASSERT_TRUE(WaitForClientEvent(client, NrClientEventKind::TransportConnectionFailed, failedEvent));
            NrStatus transportStatus;
            ASSERT_TRUE(failedEvent.GetTransportStatus(&transportStatus).Succeeded());
            EXPECT_TRUE(transportStatus.Failed());
            EXPECT_TRUE(client.Shutdown().Succeeded());
        }

        TEST(NrClientTests, InvalidCapturePreservesPreviouslyCapturedSnapshot)
        {
            NrClient validClient;
            ASSERT_TRUE(NrClient::Create(NrClientConfig{}, &validClient).Succeeded());

            NrClientSnapshot snapshot;
            ASSERT_TRUE(validClient.CaptureSnapshot(&snapshot).Succeeded());
            ASSERT_TRUE(snapshot.IsValid());
            ASSERT_EQ(snapshot.LifecycleState(), NrClientLifecycleState::Idle);

            const NrClient invalidClient;
            EXPECT_EQ(invalidClient.CaptureSnapshot(&snapshot).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_TRUE(snapshot.IsValid());
            EXPECT_EQ(snapshot.LifecycleState(), NrClientLifecycleState::Idle);
        }
    } // namespace
} // namespace psnr::runtime
