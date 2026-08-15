#include "pch.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "ControlledEntityState.h"
#include "EntitySpawn.h"
#include "JoinWorldRequest.h"
#include "MovementInput.h"
#include "NrServerWorldEventSource.h"
#include "WorldApplicationEventSinkTestDouble.h"
#include "WorldDoubleBufferedWorkers.h"
#include "WorldEntityManager.h"
#include "WorldFixedStepSchedule.h"
#include "WorldIngressEventConsumer.h"
#include "WorldMovementCommandStore.h"
#include "WorldOutboundPublisher.h"
#include "WorldOwnerLoop.h"
#include "WorldPacketTypes.h"
#include "WorldReady.h"
#include "WorldSessionRegistry.h"
#include "WorldTimeSyncRequest.h"
#include "WorldTimeSyncResponse.h"
#include "WorldWorkerShutdown.h"
#include "WorldWorkerStartup.h"

#include <PrivateServer/NetworkRuntime/NrClient.h>
#include <PrivateServer/NetworkRuntime/NrErrorCode.h>
#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrServerConfig.h>
#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace psnr::world::tests
{
    namespace
    {
        WorldApplicationEventSinkTestDouble applicationEventSink;

        constexpr std::uint32_t WorldTickRateHz = 20;
        constexpr std::uint32_t WorldMaxCatchUpSteps = 4;
        constexpr std::uint32_t FirstServerTick = 1;
        constexpr std::size_t MaxIngressEventCountPerIteration = 64;
        constexpr std::chrono::seconds PublicLoopbackTimeout{5};
        constexpr std::uint16_t FirstCandidatePort = 27150;
        constexpr std::uint16_t CandidatePortCount = 32;
        constexpr std::uint32_t ProbeSequence = 42;

        class WorldTestWinsockRuntime final
        {
        public:
            WorldTestWinsockRuntime() noexcept
            {
                WSADATA data{};
                startResult_ = WSAStartup(MAKEWORD(2, 2), &data);
            }

            WorldTestWinsockRuntime(const WorldTestWinsockRuntime&) = delete;
            WorldTestWinsockRuntime& operator=(const WorldTestWinsockRuntime&) = delete;

            ~WorldTestWinsockRuntime() noexcept
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

        class WorldTestSocket final
        {
        public:
            WorldTestSocket() noexcept = default;

            WorldTestSocket(const WorldTestSocket&) = delete;
            WorldTestSocket& operator=(const WorldTestSocket&) = delete;

            ~WorldTestSocket() noexcept
            {
                Close();
            }

            [[nodiscard]] bool ConnectWithoutReceiving(const psnr::runtime::NrEndpoint& endpoint) noexcept
            {
                if (endpoint.addressType != psnr::runtime::NrEndpointAddressType::IPv4)
                {
                    return false;
                }

                socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (socket_ == INVALID_SOCKET)
                {
                    return false;
                }

                const int receiveBufferBytes = 1024;
                if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receiveBufferBytes),
                               sizeof(receiveBufferBytes)) == SOCKET_ERROR)
                {
                    return false;
                }

                const std::uint32_t hostAddress = (static_cast<std::uint32_t>(endpoint.ipv4Address.octets[0]) << 24) |
                                                  (static_cast<std::uint32_t>(endpoint.ipv4Address.octets[1]) << 16) |
                                                  (static_cast<std::uint32_t>(endpoint.ipv4Address.octets[2]) << 8) |
                                                  static_cast<std::uint32_t>(endpoint.ipv4Address.octets[3]);
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(hostAddress);
                address.sin_port = htons(endpoint.port);
                return connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR;
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

        const WorldIngressEventConsumerConfig ConsumerConfig{
            WorldJoinConfig{
                WorldTickRateHz,
                1,
                2,
                -100.0f,
                -100.0f,
                100.0f,
                100.0f,
                1,
                0.5f,
                5.0f,
                0.0f,
                0.0f,
                WorldPlayerBodyConfig{},
                7,
            },
            1,
        };

        constexpr std::array<psnr::core::NrPacketType, protocol::C2SWorldIngressPacketTypes.size()>
            WorldIngressPacketTypes = {
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::JoinWorldRequest)},
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput)},
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::WorldTimeSyncRequest)},
                psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::ControlStateCommand)},
        };

        [[nodiscard]] psnr::runtime::NrByteView MakeByteView(const std::span<const std::byte> payload) noexcept
        {
            return psnr::runtime::NrByteView{
                payload.data(),
                static_cast<std::uint32_t>(payload.size()),
            };
        }

        [[nodiscard]] bool TryStartServer(
            psnr::runtime::NrServer* const outServer, psnr::runtime::NrEndpoint* const outEndpoint,
            const std::size_t toWorldEventCapacity = psnr::runtime::NrDefaultToWorldEventCapacity,
            const std::size_t pendingSendQueueCapacity = psnr::runtime::NrDefaultPendingSendQueueCapacity,
            const std::size_t actorMailboxCapacity = psnr::runtime::NrDefaultActorMailboxCapacity) noexcept
        {
            if (outServer == nullptr || outEndpoint == nullptr)
            {
                return false;
            }

            for (std::uint16_t offset = 0; offset < CandidatePortCount; ++offset)
            {
                psnr::runtime::NrServerConfig runtimeConfig;
                runtimeConfig.bindEndpoint.port = static_cast<std::uint16_t>(FirstCandidatePort + offset);
                runtimeConfig.toWorldEventCapacity = toWorldEventCapacity;
                runtimeConfig.pendingSendQueueCapacity = pendingSendQueueCapacity;
                runtimeConfig.actorMailboxCapacity = actorMailboxCapacity;
                runtimeConfig.additionalWorldIngressPacketTypes = psnr::runtime::NrPacketTypeView{
                    WorldIngressPacketTypes.data(),
                    static_cast<std::uint32_t>(WorldIngressPacketTypes.size()),
                };

                psnr::runtime::NrServer candidate;
                if (psnr::runtime::NrServer::Create(runtimeConfig, &candidate).Failed())
                {
                    return false;
                }
                if (candidate.Start().Failed())
                {
                    continue;
                }

                *outEndpoint = runtimeConfig.bindEndpoint;
                *outServer = std::move(candidate);
                return true;
            }

            return false;
        }

        [[nodiscard]] bool WaitForServerAcceptedSendChannel(
            psnr::runtime::NrServer& server, psnr::runtime::NrSessionSendChannel* const outChannel) noexcept
        {
            if (outChannel == nullptr)
            {
                return false;
            }

            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrToWorldEvent event;
                const psnr::core::NrStatus popStatus = server.TryPopToWorldEvent(&event);
                if (popStatus.Succeeded())
                {
                    if (event.Kind() == psnr::runtime::NrToWorldEventKind::SessionAccepted)
                    {
                        return event.GetSendChannel(outChannel).Succeeded();
                    }
                    continue;
                }
                if (popStatus.ErrorCode() != psnr::core::NrErrorCode::QueueEmpty)
                {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return false;
        }

        [[nodiscard]] bool WaitForServerToWorldDepth(psnr::runtime::NrServer& server,
                                                     const std::uint64_t minimumDepth) noexcept
        {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrServerSnapshot snapshot;
                if (server.CaptureSnapshot(&snapshot).Succeeded() && snapshot.ToWorldEventDepth() >= minimumDepth)
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return false;
        }

        [[nodiscard]] bool WaitForServerSessionClosedReason(
            psnr::runtime::NrServer& server, psnr::runtime::NrSessionEndReason* const outEndReason) noexcept
        {
            if (outEndReason == nullptr)
            {
                return false;
            }

            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrToWorldEvent event;
                const psnr::core::NrStatus popStatus = server.TryPopToWorldEvent(&event);
                if (popStatus.Succeeded())
                {
                    if (event.Kind() == psnr::runtime::NrToWorldEventKind::SessionClosed)
                    {
                        return event.GetEndReason(*outEndReason).Succeeded();
                    }
                    continue;
                }
                if (popStatus.ErrorCode() != psnr::core::NrErrorCode::QueueEmpty)
                {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return false;
        }

        [[nodiscard]] bool TryPopClientEventUntil(psnr::runtime::NrClient& client,
                                                  const std::chrono::steady_clock::time_point deadline,
                                                  psnr::runtime::NrClientEvent* const outEvent) noexcept
        {
            if (outEvent == nullptr)
            {
                return false;
            }

            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrClientEvent event;
                const psnr::core::NrStatus popStatus = client.TryPopEvent(&event);
                if (popStatus.Succeeded())
                {
                    *outEvent = std::move(event);
                    return true;
                }
                if (popStatus.ErrorCode() != psnr::core::NrErrorCode::QueueEmpty)
                {
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return false;
        }

        [[nodiscard]] bool WaitForClientEventKind(psnr::runtime::NrClient& client,
                                                  const psnr::runtime::NrClientEventKind expectedKind) noexcept
        {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrClientEvent event;
                if (!TryPopClientEventUntil(client, deadline, &event))
                {
                    return false;
                }
                if (event.Kind() == expectedKind)
                {
                    return true;
                }
                if (event.Kind() == psnr::runtime::NrClientEventKind::TransportConnectionFailed)
                {
                    return false;
                }
            }

            return false;
        }

        [[nodiscard]] bool TryGetPacketPayload(const psnr::runtime::NrClientEvent& event,
                                               psnr::core::NrPacketType* const outPacketType,
                                               std::span<const std::byte>* const outPayload) noexcept
        {
            if (outPacketType == nullptr || outPayload == nullptr ||
                event.Kind() != psnr::runtime::NrClientEventKind::PacketReceived)
            {
                return false;
            }

            psnr::runtime::NrByteView payload;
            if (event.GetPacketType(outPacketType).Failed() || event.GetPayload(&payload).Failed())
            {
                return false;
            }

            *outPayload = std::span<const std::byte>(payload.data, payload.size);
            return true;
        }

        [[nodiscard]] bool WaitForJoinBaseline(psnr::runtime::NrClient& client, protocol::v2::EntitySpawn* outSpawn,
                                               protocol::v2::WorldReady* outReady) noexcept
        {
            if (outSpawn == nullptr || outReady == nullptr)
            {
                return false;
            }

            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            bool spawnReceived = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrClientEvent event;
                if (!TryPopClientEventUntil(client, deadline, &event))
                {
                    return false;
                }
                if (event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
                {
                    return false;
                }
                if (event.Kind() != psnr::runtime::NrClientEventKind::PacketReceived)
                {
                    continue;
                }

                psnr::core::NrPacketType packetType;
                std::span<const std::byte> payload;
                if (!TryGetPacketPayload(event, &packetType, &payload))
                {
                    return false;
                }

                if (packetType.value == static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn))
                {
                    if (protocol::v2::EntitySpawn::Decode(payload, outSpawn) != protocol::WorldProtocolError::Success)
                    {
                        return false;
                    }
                    spawnReceived = true;
                }
                else if (packetType.value == static_cast<std::uint16_t>(protocol::S2CPacketType::WorldReady))
                {
                    return spawnReceived &&
                           protocol::v2::WorldReady::Decode(payload, outReady) == protocol::WorldProtocolError::Success;
                }
            }

            return false;
        }

        [[nodiscard]] bool WaitForTimeSyncResponse(psnr::runtime::NrClient& client,
                                                   protocol::v1::WorldTimeSyncResponse* const outResponse) noexcept
        {
            if (outResponse == nullptr)
            {
                return false;
            }

            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrClientEvent event;
                if (!TryPopClientEventUntil(client, deadline, &event))
                {
                    return false;
                }
                if (event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
                {
                    return false;
                }
                if (event.Kind() != psnr::runtime::NrClientEventKind::PacketReceived)
                {
                    continue;
                }

                psnr::core::NrPacketType packetType;
                std::span<const std::byte> payload;
                if (!TryGetPacketPayload(event, &packetType, &payload))
                {
                    return false;
                }
                if (packetType.value != static_cast<std::uint16_t>(protocol::S2CPacketType::WorldTimeSyncResponse))
                {
                    continue;
                }

                return protocol::v1::WorldTimeSyncResponse::Decode(payload, outResponse) ==
                       protocol::WorldProtocolError::Success;
            }

            return false;
        }

        [[nodiscard]] bool WaitForMovedState(psnr::runtime::NrClient& client, const std::uint32_t targetServerTick,
                                             const std::uint32_t generation, const float initialPositionX,
                                             protocol::v1::ControlledEntityState* const outState) noexcept
        {
            if (outState == nullptr)
            {
                return false;
            }

            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + PublicLoopbackTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                psnr::runtime::NrClientEvent event;
                if (!TryPopClientEventUntil(client, deadline, &event))
                {
                    return false;
                }
                if (event.Kind() == psnr::runtime::NrClientEventKind::TransportDisconnected)
                {
                    return false;
                }
                if (event.Kind() != psnr::runtime::NrClientEventKind::PacketReceived)
                {
                    continue;
                }

                psnr::core::NrPacketType packetType;
                std::span<const std::byte> payload;
                if (!TryGetPacketPayload(event, &packetType, &payload) ||
                    packetType.value != static_cast<std::uint16_t>(protocol::S2CPacketType::ControlledEntityState))
                {
                    continue;
                }

                protocol::v1::ControlledEntityState state;
                if (protocol::v1::ControlledEntityState::Decode(payload, &state) !=
                    protocol::WorldProtocolError::Success)
                {
                    return false;
                }
                if (state.serverTick >= targetServerTick && state.controlledEntityGeneration == generation &&
                    state.positionX > initialPositionX)
                {
                    *outState = state;
                    return true;
                }
            }

            return false;
        }

        class PublicLoopbackCleanup final
        {
        public:
            PublicLoopbackCleanup(psnr::runtime::NrClient& client, psnr::runtime::NrServer& server,
                                  std::thread& worldThread) noexcept
                : client_(client)
                , server_(server)
                , worldThread_(worldThread)
            {
            }

            PublicLoopbackCleanup(const PublicLoopbackCleanup&) = delete;
            PublicLoopbackCleanup& operator=(const PublicLoopbackCleanup&) = delete;

            ~PublicLoopbackCleanup() noexcept
            {
                Stop();
            }

            void Stop() noexcept
            {
                if (!active_)
                {
                    return;
                }

                if (client_.IsValid())
                {
                    const psnr::core::NrStatus disconnectStatus = client_.Disconnect();
                    const psnr::core::NrStatus clientShutdownStatus = client_.Shutdown();
                    static_cast<void>(disconnectStatus);
                    static_cast<void>(clientShutdownStatus);
                }
                if (server_.IsValid())
                {
                    const psnr::core::NrStatus requestStopStatus = server_.RequestStop();
                    const psnr::core::NrStatus serverShutdownStatus = server_.Shutdown();
                    static_cast<void>(requestStopStatus);
                    static_cast<void>(serverShutdownStatus);
                }
                if (worldThread_.joinable())
                {
                    worldThread_.join();
                }

                active_ = false;
            }

        private:
            psnr::runtime::NrClient& client_;
            psnr::runtime::NrServer& server_;
            std::thread& worldThread_;
            bool active_ = true;
        };

        class PublicRuntimeShutdownAdapter final
        {
        public:
            explicit PublicRuntimeShutdownAdapter(psnr::runtime::NrServer& server) noexcept
                : server_(server)
            {
            }

            [[nodiscard]] bool RequestStopAndShutdown() noexcept
            {
                return server_.RequestStop().Succeeded() && server_.Shutdown().Succeeded();
            }

        private:
            psnr::runtime::NrServer& server_;
        };

        class DoubleBufferedLoopbackCleanup final
        {
        public:
            DoubleBufferedLoopbackCleanup(psnr::runtime::NrClient& client, psnr::runtime::NrServer& server,
                                          WorldOutboundPublisherWorker& publisherWorker,
                                          WorldIngressPumpWorker& pumpWorker,
                                          WorldDoubleBufferedCoordinatorWorker& coordinatorWorker) noexcept
                : client_(client)
                , server_(server)
                , publisherWorker_(publisherWorker)
                , pumpWorker_(pumpWorker)
                , coordinatorWorker_(coordinatorWorker)
            {
            }

            DoubleBufferedLoopbackCleanup(const DoubleBufferedLoopbackCleanup&) = delete;
            DoubleBufferedLoopbackCleanup& operator=(const DoubleBufferedLoopbackCleanup&) = delete;

            ~DoubleBufferedLoopbackCleanup() noexcept
            {
                Stop();
            }

            void MarkWorkersStarted() noexcept
            {
                workersStarted_ = true;
            }

            void Release() noexcept
            {
                active_ = false;
            }

        private:
            void Stop() noexcept
            {
                if (!active_)
                {
                    return;
                }

                if (client_.IsValid())
                {
                    const psnr::core::NrStatus disconnectStatus = client_.Disconnect();
                    static_cast<void>(disconnectStatus);
                }

                if (workersStarted_)
                {
                    PublicRuntimeShutdownAdapter runtime{server_};
                    const WorldExecutionModeConfig modes{
                        WorldInboundMode::DoubleBuffered,
                        WorldOutboundMode::DoubleBuffered,
                    };
                    const WorldWorkerShutdownReport shutdownReport =
                        WorldWorkerShutdown::Run(modes, runtime, &publisherWorker_, &pumpWorker_, coordinatorWorker_);
                    static_cast<void>(shutdownReport);
                }
                else if (server_.IsValid())
                {
                    const psnr::core::NrStatus requestStopStatus = server_.RequestStop();
                    const psnr::core::NrStatus serverShutdownStatus = server_.Shutdown();
                    static_cast<void>(requestStopStatus);
                    static_cast<void>(serverShutdownStatus);
                }

                if (client_.IsValid())
                {
                    const psnr::core::NrStatus clientShutdownStatus = client_.Shutdown();
                    static_cast<void>(clientShutdownStatus);
                }
                active_ = false;
            }

            psnr::runtime::NrClient& client_;
            psnr::runtime::NrServer& server_;
            WorldOutboundPublisherWorker& publisherWorker_;
            WorldIngressPumpWorker& pumpWorker_;
            WorldDoubleBufferedCoordinatorWorker& coordinatorWorker_;
            bool workersStarted_ = false;
            bool active_ = true;
        };
    } // namespace

    TEST(WorldPublicLoopbackTests, ClientJoinsMovesReceivesStateAndShutsDownCleanly)
    {
        psnr::runtime::NrServer server;
        psnr::runtime::NrEndpoint endpoint;
        ASSERT_TRUE(TryStartServer(&server, &endpoint));

        psnr::runtime::NrGateway gateway;
        ASSERT_TRUE(server.CreateGateway(&gateway).Succeeded());

        WorldResult<WorldFixedStepSchedule> scheduleResult = CreateWorldFixedStepSchedule(
            WorldFixedStepScheduleConfig{
                WorldTickRateHz,
                WorldMaxCatchUpSteps,
                FirstServerTick,
            },
            WorldFixedStepSchedule::Clock::now());
        ASSERT_TRUE(scheduleResult.Succeeded());
        WorldFixedStepSchedule schedule = scheduleResult.TakeValue();

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementCommandStore;
        NrServerWorldEventSource eventSource(server);
        WorldIngressEventConsumer eventConsumer(sessionRegistry, entityManager, movementCommandStore, server, gateway,
                                                ConsumerConfig, FirstServerTick, FirstServerTick - 1,
                                                applicationEventSink);
        WorldOwnerLoop ownerLoop(std::move(schedule), sessionRegistry, movementCommandStore, entityManager,
                                 MaxIngressEventCountPerIteration, FirstServerTick - 1);
        WorldSteadyClockSource clockSource;
        WorldOwnerStepReport terminalWorldReport;

        std::thread worldThread(
            [&eventSource, &eventConsumer, &ownerLoop, &clockSource, &terminalWorldReport]() noexcept
            {
                do
                {
                    terminalWorldReport = ownerLoop.RunNext(eventSource, eventConsumer, clockSource);
                } while (terminalWorldReport.stopReason == WorldOwnerStepStopReason::IterationCompleted);
            });

        psnr::runtime::NrClient client;
        PublicLoopbackCleanup cleanup(client, server, worldThread);

        ASSERT_TRUE(psnr::runtime::NrClient::Create(psnr::runtime::NrClientConfig{}, &client).Succeeded());
        ASSERT_TRUE(client.Connect(endpoint).Succeeded());
        ASSERT_TRUE(WaitForClientEventKind(client, psnr::runtime::NrClientEventKind::TransportConnected));

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_TRUE(
            client
                .Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::JoinWorldRequest)},
                      MakeByteView(joinPayload))
                .Succeeded());

        protocol::v2::EntitySpawn spawn;
        protocol::v2::WorldReady ready;
        ASSERT_TRUE(WaitForJoinBaseline(client, &spawn, &ready));
        EXPECT_EQ(spawn.baseline.entityId, ready.controlledEntityId);
        EXPECT_EQ(spawn.baseline.generation, ready.controlledEntityGeneration);
        EXPECT_EQ(spawn.baseline.serverTick, ready.currentServerTick);
        EXPECT_EQ(spawn.playerId, ready.playerId);
        EXPECT_EQ(ready.channelId, 7u);
        EXPECT_TRUE(ready.displayName.empty());

        std::array<std::byte, protocol::v1::WorldTimeSyncRequest::Wire::PayloadBytes> timeSyncPayload;
        ASSERT_EQ(protocol::v1::WorldTimeSyncRequest::Encode(protocol::v1::WorldTimeSyncRequest{ProbeSequence},
                                                             timeSyncPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_TRUE(client
                        .Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(
                                  protocol::C2SPacketType::WorldTimeSyncRequest)},
                              MakeByteView(timeSyncPayload))
                        .Succeeded());

        protocol::v1::WorldTimeSyncResponse timeSyncResponse;
        ASSERT_TRUE(WaitForTimeSyncResponse(client, &timeSyncResponse));
        ASSERT_EQ(timeSyncResponse.probeSequence, ProbeSequence);

        const std::uint32_t targetServerTick = timeSyncResponse.serverTick + 3;
        const protocol::v1::MovementInput movement{
            ready.controlledEntityGeneration,
            targetServerTick,
            32767,
            0,
        };
        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> movementPayload;
        ASSERT_EQ(protocol::v1::MovementInput::Encode(movement, movementPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_TRUE(
            client
                .Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput)},
                      MakeByteView(movementPayload))
                .Succeeded());

        protocol::v1::ControlledEntityState movedState;
        ASSERT_TRUE(WaitForMovedState(client, targetServerTick, ready.controlledEntityGeneration,
                                      spawn.baseline.positionX, &movedState));
        EXPECT_GT(movedState.velocityX, 0.0f);
        EXPECT_FLOAT_EQ(movedState.positionY, spawn.baseline.positionY);

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes - 1> malformedMovementPayload{};
        ASSERT_TRUE(
            client
                .Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput)},
                      MakeByteView(malformedMovementPayload))
                .Succeeded());
        ASSERT_TRUE(WaitForClientEventKind(client, psnr::runtime::NrClientEventKind::TransportDisconnected));
        ASSERT_TRUE(client.Shutdown().Succeeded());
        ASSERT_TRUE(server.RequestStop().Succeeded());
        ASSERT_TRUE(server.Shutdown().Succeeded());
        worldThread.join();
        cleanup.Stop();

        EXPECT_EQ(terminalWorldReport.stopReason, WorldOwnerStepStopReason::IngressClosed);
        EXPECT_EQ(sessionRegistry.Size(), 0u);
        EXPECT_EQ(entityManager.Size(), 0u);
        EXPECT_EQ(eventConsumer.Metrics().malformedPayloadCount, 1u);
        EXPECT_EQ(eventConsumer.Metrics().protocolCloseRequestCount, 1u);
        EXPECT_EQ(eventConsumer.Metrics().protocolCloseRequestSuccessCount, 1u);
        EXPECT_EQ(eventConsumer.Metrics().protocolCloseRequestFailureCount, 0u);
        EXPECT_EQ(eventConsumer.Metrics().protocolErrorSessionClosedCount, 1u);
    }

    TEST(WorldPublicLoopbackTests, IngressBurstEndsSaturatedSessionWithObservableReceivePressure)
    {
        constexpr std::size_t ToWorldEventCapacity = 2;
        constexpr std::size_t BurstPacketCount = 16;
        psnr::runtime::NrServer server;
        psnr::runtime::NrEndpoint endpoint;
        ASSERT_TRUE(TryStartServer(&server, &endpoint, ToWorldEventCapacity));

        psnr::runtime::NrClient client;
        ASSERT_TRUE(psnr::runtime::NrClient::Create(psnr::runtime::NrClientConfig{}, &client).Succeeded());
        ASSERT_TRUE(client.Connect(endpoint).Succeeded());
        ASSERT_TRUE(WaitForClientEventKind(client, psnr::runtime::NrClientEventKind::TransportConnected));
        ASSERT_TRUE(WaitForServerToWorldDepth(server, 1));

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);

        std::size_t submittedPacketCount = 0;
        for (std::size_t index = 0; index < BurstPacketCount; ++index)
        {
            const psnr::core::NrStatus sendStatus = client.Send(
                psnr::core::NrPacketType{
                    static_cast<std::uint16_t>(protocol::C2SPacketType::JoinWorldRequest),
                },
                MakeByteView(joinPayload));
            if (sendStatus.Failed())
            {
                break;
            }
            ++submittedPacketCount;
        }
        ASSERT_GE(submittedPacketCount, ToWorldEventCapacity);
        ASSERT_TRUE(WaitForClientEventKind(client, psnr::runtime::NrClientEventKind::TransportDisconnected));

        psnr::runtime::NrServerSnapshot pressureSnapshot;
        ASSERT_TRUE(server.CaptureSnapshot(&pressureSnapshot).Succeeded());
        EXPECT_EQ(pressureSnapshot.ToWorldEventHighWatermark(), ToWorldEventCapacity);
        EXPECT_GE(pressureSnapshot.PressureTransactionCount(
                      psnr::runtime::NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected),
                  1u);
        EXPECT_EQ(pressureSnapshot.PressureTransactionCount(
                      psnr::runtime::NrPressureTransactionOutcome::ReceivePressureCloseCommitted),
                  1u);

        psnr::runtime::NrSessionEndReason endReason = psnr::runtime::NrSessionEndReason::None;
        ASSERT_TRUE(WaitForServerSessionClosedReason(server, &endReason));
        EXPECT_EQ(endReason, psnr::runtime::NrSessionEndReason::ReceivePressure);

        ASSERT_TRUE(client.Shutdown().Succeeded());
        ASSERT_TRUE(server.RequestStop().Succeeded());
        ASSERT_TRUE(server.Shutdown().Succeeded());
    }

    TEST(WorldPublicLoopbackTests, SlowClientOutboundPressureEndsAcceptedPublicationWithSendPressure)
    {
        constexpr std::size_t PendingSendQueueCapacity = 1;
        constexpr std::size_t ActorMailboxCapacity = 256;
        constexpr std::size_t OutboundRecordCount = 128;
        constexpr std::size_t OutboundPayloadBytes = 8000;

        WorldTestWinsockRuntime winsockRuntime;
        ASSERT_TRUE(winsockRuntime.Started());

        psnr::runtime::NrServer server;
        psnr::runtime::NrEndpoint endpoint;
        ASSERT_TRUE(TryStartServer(&server, &endpoint, psnr::runtime::NrDefaultToWorldEventCapacity,
                                   PendingSendQueueCapacity, ActorMailboxCapacity));

        psnr::runtime::NrGateway gateway;
        ASSERT_TRUE(server.CreateGateway(&gateway).Succeeded());

        WorldTestSocket slowClient;
        ASSERT_TRUE(slowClient.ConnectWithoutReceiving(endpoint));

        psnr::runtime::NrSessionSendChannel sendChannel;
        ASSERT_TRUE(WaitForServerAcceptedSendChannel(server, &sendChannel));

        WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> outboundBufferResult =
            WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{
                OutboundRecordCount,
                OutboundRecordCount,
                OutboundRecordCount * OutboundPayloadBytes,
            });
        ASSERT_TRUE(outboundBufferResult.Succeeded());
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer = outboundBufferResult.TakeValue();
        ASSERT_NE(outboundBuffer, nullptr);

        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{sendChannel};
        const std::vector<std::byte> payload(OutboundPayloadBytes, std::byte{0x2A});
        for (std::size_t index = 0; index < OutboundRecordCount; ++index)
        {
            ASSERT_EQ(outboundBuffer->TryAppend(psnr::core::NrPacketType{0x7FFE}, recipients, payload),
                      WorldOutboundAppendResult::Appended);
        }
        ASSERT_EQ(outboundBuffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundPublisher publisher{*outboundBuffer};
        const WorldOutboundPublishReport report = publisher.PublishNext(gateway, std::chrono::milliseconds::zero());
        EXPECT_EQ(report.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(report.recordCount, OutboundRecordCount);
        EXPECT_EQ(report.processedRecordCount, OutboundRecordCount);
        EXPECT_EQ(report.discardedRecordCount, 0u);
        EXPECT_EQ(report.attemptedRecipientCount, OutboundRecordCount);
        EXPECT_EQ(report.acceptedRecipientCount + report.rejectedRecipientCount, OutboundRecordCount);
        EXPECT_EQ(report.discardedRecipientCount, 0u);
        EXPECT_GT(report.acceptedRecipientCount, 0u);

        psnr::runtime::NrSessionEndReason endReason = psnr::runtime::NrSessionEndReason::None;
        ASSERT_TRUE(WaitForServerSessionClosedReason(server, &endReason));
        EXPECT_EQ(endReason, psnr::runtime::NrSessionEndReason::SendPressure);

        psnr::runtime::NrServerSnapshot pressureSnapshot;
        ASSERT_TRUE(server.CaptureSnapshot(&pressureSnapshot).Succeeded());
        EXPECT_GE(pressureSnapshot.PressureTransactionCount(
                      psnr::runtime::NrPressureTransactionOutcome::SendAdmissionRejected),
                  1u);
        EXPECT_EQ(pressureSnapshot.PressureTransactionCount(
                      psnr::runtime::NrPressureTransactionOutcome::SendPressureCloseCommitted),
                  1u);

        slowClient.Close();
        ASSERT_TRUE(server.RequestStop().Succeeded());
        ASSERT_TRUE(server.Shutdown().Succeeded());
    }

    TEST(WorldPublicLoopbackTests, DoubleBufferedMovementLoadPreservesTerminalCountsAndJoinsOnShutdown)
    {
        psnr::runtime::NrServer server;
        psnr::runtime::NrEndpoint endpoint;
        ASSERT_TRUE(TryStartServer(&server, &endpoint));

        psnr::runtime::NrGateway gateway;
        ASSERT_TRUE(server.CreateGateway(&gateway).Succeeded());

        WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> ingressBufferResult =
            WorldIngressDoubleBuffer::Create(128);
        ASSERT_TRUE(ingressBufferResult.Succeeded());
        std::unique_ptr<WorldIngressDoubleBuffer> ingressBuffer = ingressBufferResult.TakeValue();
        WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> outboundBufferResult =
            WorldOutboundDoubleBuffer::Create(WorldOutboundBatchCapacity{256, 1024, 64 * 1024},
                                              WorldOutboundBufferSlotCount::Triple);
        ASSERT_TRUE(outboundBufferResult.Succeeded());
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer = outboundBufferResult.TakeValue();
        ASSERT_EQ(outboundBuffer->ConfiguredSlotCount(), 3u);

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementCommandStore;
        NrServerWorldEventSource eventSource{server};
        WorldIngressEventConsumer eventConsumer{
            sessionRegistry,
            entityManager,
            movementCommandStore,
            server,
            gateway,
            WorldOutboundMode::DoubleBuffered,
            outboundBuffer.get(),
            ConsumerConfig,
            FirstServerTick,
            FirstServerTick - 1,
            applicationEventSink,
        };

        const std::chrono::nanoseconds fixedStep{1000000000ull / WorldTickRateHz};
        const WorldDoubleBufferedTickConfig tickConfig{
            fixedStep,
            WorldMaxCatchUpSteps,
            1,
            FirstServerTick,
            FirstServerTick - 1,
            WorldClock::now() + fixedStep,
            WorldOutboundMode::DoubleBuffered,
        };
        WorldIngressPump pump{*ingressBuffer};
        WorldOutboundPublisher publisher{*outboundBuffer};
        WorldIngressWorkerExchange ingressExchange;
        WorldDoubleBufferedTickCoordinator coordinator{
            tickConfig, *ingressBuffer, sessionRegistry, movementCommandStore, entityManager, outboundBuffer.get(),
        };
        WorldOutboundPublisherWorker publisherWorker{publisher, gateway};
        WorldIngressPumpWorker pumpWorker{pump, eventSource, ingressExchange};
        WorldDoubleBufferedCoordinatorWorker coordinatorWorker{
            WorldDoubleBufferedCoordinatorWorkerConfig{std::chrono::seconds{2}},
            coordinator,
            *ingressBuffer,
            eventConsumer,
            ingressExchange,
        };
        const WorldExecutionModeConfig modes{
            WorldInboundMode::DoubleBuffered,
            WorldOutboundMode::DoubleBuffered,
        };

        psnr::runtime::NrClient client;
        DoubleBufferedLoopbackCleanup cleanup(client, server, publisherWorker, pumpWorker, coordinatorWorker);
        ASSERT_EQ(WorldWorkerStartup::Start(modes, &publisherWorker, &pumpWorker, coordinatorWorker).result,
                  WorldWorkerStartupResult::Started);
        cleanup.MarkWorkersStarted();

        ASSERT_TRUE(psnr::runtime::NrClient::Create(psnr::runtime::NrClientConfig{}, &client).Succeeded());
        ASSERT_TRUE(client.Connect(endpoint).Succeeded());
        ASSERT_TRUE(WaitForClientEventKind(client, psnr::runtime::NrClientEventKind::TransportConnected));

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_TRUE(
            client
                .Send(psnr::core::NrPacketType{static_cast<std::uint16_t>(protocol::C2SPacketType::JoinWorldRequest)},
                      MakeByteView(joinPayload))
                .Succeeded());

        protocol::v2::EntitySpawn spawn;
        protocol::v2::WorldReady ready;
        ASSERT_TRUE(WaitForJoinBaseline(client, &spawn, &ready));
        EXPECT_EQ(spawn.baseline.entityId, ready.controlledEntityId);
        EXPECT_EQ(spawn.baseline.generation, ready.controlledEntityGeneration);
        EXPECT_EQ(spawn.playerId, ready.playerId);
        EXPECT_EQ(ready.channelId, 7u);
        EXPECT_TRUE(ready.displayName.empty());

        const protocol::v1::MovementInput movement{
            ready.controlledEntityGeneration,
            ready.currentServerTick + 1,
            32767,
            0,
        };
        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> movementPayload;
        ASSERT_EQ(protocol::v1::MovementInput::Encode(movement, movementPayload),
                  protocol::WorldProtocolError::Success);

        std::atomic<bool> stopLoad{false};
        std::atomic_size_t submittedMovementCount{0};
        std::thread loadThread(
            [&client, &movementPayload, &stopLoad, &submittedMovementCount]() noexcept
            {
                while (!stopLoad.load(std::memory_order_acquire))
                {
                    const psnr::core::NrStatus sendStatus = client.Send(
                        psnr::core::NrPacketType{
                            static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput),
                        },
                        MakeByteView(movementPayload));
                    if (sendStatus.Failed())
                    {
                        break;
                    }
                    submittedMovementCount.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            });
        const std::chrono::steady_clock::time_point loadDeadline =
            std::chrono::steady_clock::now() + PublicLoopbackTimeout;
        while (submittedMovementCount.load(std::memory_order_acquire) < 32 &&
               std::chrono::steady_clock::now() < loadDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        PublicRuntimeShutdownAdapter runtime{server};
        const WorldWorkerShutdownReport shutdownReport =
            WorldWorkerShutdown::Run(modes, runtime, &publisherWorker, &pumpWorker, coordinatorWorker);
        stopLoad.store(true, std::memory_order_release);
        loadThread.join();
        const psnr::core::NrStatus clientShutdownStatus = client.Shutdown();
        cleanup.Release();

        EXPECT_TRUE(clientShutdownStatus.Succeeded());
        EXPECT_GE(submittedMovementCount.load(std::memory_order_acquire), 32u);
        const WorldIngressPumpMetrics ingressMetrics = pump.Metrics();
        const WorldDoubleBufferedTickMetrics tickMetrics = coordinator.Metrics();
        const WorldOutboundPublisherMetrics outboundMetrics = publisher.Metrics();
        EXPECT_EQ(shutdownReport.result, WorldWorkerShutdownResult::Completed);
        EXPECT_GT(ingressMetrics.drainedEventCount, 0u);
        EXPECT_GT(tickMetrics.processedTickCount, 0u);
        EXPECT_GT(tickMetrics.publishedSnapshotCount, 0u);
        EXPECT_LE(tickMetrics.currentBacklogTickCount, tickMetrics.maximumBacklogTickCount);
        EXPECT_GT(outboundMetrics.publishedBatchCount, 0u);
        EXPECT_GE(outboundMetrics.publishedRecordCount, 2u);
        EXPECT_TRUE(shutdownReport.gameplayStopped);
        EXPECT_TRUE(shutdownReport.outboundDrained);
        EXPECT_TRUE(shutdownReport.terminalIngressEnabled);
        EXPECT_TRUE(shutdownReport.terminalIngressDrained);
        EXPECT_TRUE(shutdownReport.runtimeShutdownRequested);
        EXPECT_TRUE(shutdownReport.runtimeShutdownSucceeded);
        EXPECT_TRUE(shutdownReport.workersJoined);
        EXPECT_EQ(shutdownReport.terminalIngress.closedEventCount, 1u);
        EXPECT_EQ(publisherWorker.StopReason(), WorldConcreteWorkerStopReason::Completed);
        EXPECT_EQ(pumpWorker.StopReason(), WorldConcreteWorkerStopReason::Completed);
        EXPECT_EQ(coordinatorWorker.StopReason(), WorldConcreteWorkerStopReason::Completed);
        EXPECT_EQ(sessionRegistry.Size(), 0u);
        EXPECT_EQ(entityManager.Size(), 0u);
    }
} // namespace psnr::world::tests
