#include "pch.h"

#include "ControlStateCommand.h"
#include "ControlledEntityState.h"
#include "ControlledEntityRebind.h"
#include "EntitySpawn.h"
#include "EntityStateBatch.h"
#include "JoinWorldRequest.h"
#include "MovementInput.h"
#include "ObserverReady.h"
#include "ObserveWorldRequest.h"
#include "WorldApplicationEventSinkTestDouble.h"
#include "WorldIngressEventConsumer.h"
#include "WorldOutboundPublisher.h"
#include "WorldPacketTypes.h"
#include "WorldTimeSyncRequest.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] std::unique_ptr<WorldOutboundDoubleBuffer> CreateOutboundBuffer(
            const WorldOutboundBatchCapacity capacity)
        {
            WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> result =
                WorldOutboundDoubleBuffer::Create(capacity);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        class FakeToWorldEvent final
        {
        public:
            psnr::runtime::NrToWorldEventKind kind = psnr::runtime::NrToWorldEventKind::None;
            psnr::core::NrSessionKey sessionKey = 0;
            psnr::core::NrPacketType packetType{};
            psnr::runtime::NrByteView payload{};
            psnr::runtime::NrSessionEndReason endReason = psnr::runtime::NrSessionEndReason::None;

            [[nodiscard]] psnr::runtime::NrToWorldEventKind Kind() const noexcept
            {
                return kind;
            }

            [[nodiscard]] psnr::core::NrSessionKey SessionKey() const noexcept
            {
                return sessionKey;
            }

            [[nodiscard]] psnr::core::NrStatus GetSendChannel(
                psnr::runtime::NrSessionSendChannel* const outChannel) const noexcept
            {
                if (outChannel == nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
                }
                *outChannel = psnr::runtime::NrSessionSendChannel{};
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus GetPacketType(
                psnr::core::NrPacketType* const outPacketType) const noexcept
            {
                if (outPacketType == nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
                }
                *outPacketType = packetType;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus GetPayload(psnr::runtime::NrByteView* const outPayload) const noexcept
            {
                if (outPayload == nullptr)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
                }
                *outPayload = payload;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus GetEndReason(
                psnr::runtime::NrSessionEndReason& outEndReason) const noexcept
            {
                outEndReason = endReason;
                return psnr::core::NrStatus::Success();
            }
        };

        [[nodiscard]] FakeToWorldEvent MakeAcceptedEvent(const psnr::core::NrSessionKey sessionKey) noexcept
        {
            FakeToWorldEvent event;
            event.kind = psnr::runtime::NrToWorldEventKind::SessionAccepted;
            event.sessionKey = sessionKey;
            return event;
        }

        [[nodiscard]] FakeToWorldEvent MakePacketEvent(const psnr::core::NrSessionKey sessionKey,
                                                       const protocol::C2SPacketType packetType,
                                                       const std::byte* const payload,
                                                       const std::uint32_t payloadSize) noexcept
        {
            FakeToWorldEvent event;
            event.kind = psnr::runtime::NrToWorldEventKind::PacketReceived;
            event.sessionKey = sessionKey;
            event.packetType = psnr::core::NrPacketType{static_cast<std::uint16_t>(packetType)};
            event.payload = psnr::runtime::NrByteView{payload, payloadSize};
            return event;
        }

        class FakeOutboundGateway final
        {
        public:
            [[nodiscard]] psnr::core::NrStatus SubmitMany(psnr::runtime::NrSessionSendChannelView,
                                                          const psnr::core::NrPacketType packetType,
                                                          const psnr::runtime::NrByteView payload,
                                                          psnr::runtime::NrGatewaySendReport& outReport)
            {
                packetTypes.push_back(packetType.value);
                payloads.push_back(std::vector<std::byte>{payload.data, payload.data + payload.size});
                outReport = psnr::runtime::NrGatewaySendReport{1, 1, 0};
                return psnr::core::NrStatus::Success();
            }

            std::vector<std::uint16_t> packetTypes;
            std::vector<std::vector<std::byte>> payloads;
        };

        const WorldIngressEventConsumerConfig ConsumerConfig{
            WorldJoinConfig{
                20,
                3,
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
        WorldApplicationEventSinkTestDouble applicationEventSink;
    } // namespace

    TEST(WorldIngressEventConsumerTests, AppliesLifecycleAndRoutesMovementIntoOwnedStore)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        constexpr std::uint32_t ServerTick = 100;
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        applicationEventSink.Clear();
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, ServerTick, 99, applicationEventSink);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        WorldEntityComponents components;
        components.playerControl.playerId = 7;
        WorldEntityKey entityKey;
        EntityHandle entityHandle;
        ASSERT_TRUE(entityManager.TryCreate(components, &entityKey, &entityHandle));
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, entityKey));
        ASSERT_EQ(consumer.SessionChannelCount(), 1u);

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> payload;
        const protocol::v1::MovementInput movement{
            entityKey.generation,
            ServerTick,
            16384,
            0,
        };
        ASSERT_EQ(protocol::v1::MovementInput::Encode(movement, payload), protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));

        ASSERT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::MovementStored);
        payload.fill(std::byte{0});

        std::vector<WorldMovementCommand> commands;
        ASSERT_TRUE(commandStore.TryTake(ServerTick, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0].sessionKey, SessionKey);
        EXPECT_EQ(commands[0].entityKey, entityKey);

        WorldMovementCommand futureCommand = commands[0];
        futureCommand.targetServerTick = ServerTick + 8;
        ASSERT_EQ(commandStore.TryStore(futureCommand), WorldMovementCommandStoreResult::Stored);

        FakeToWorldEvent closedEvent;
        closedEvent.kind = psnr::runtime::NrToWorldEventKind::SessionClosed;
        closedEvent.sessionKey = RuntimeSessionKey;
        closedEvent.endReason = psnr::runtime::NrSessionEndReason::RemoteClosed;

        EXPECT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);
        EXPECT_EQ(sessionRegistry.Size(), 0u);
        EXPECT_EQ(entityManager.Size(), 0u);
        EXPECT_EQ(consumer.SessionChannelCount(), 0u);
        EXPECT_EQ(commandStore.Size(), 0u);
        EXPECT_EQ(commandStore.Metrics().canceledCommandCount, 1u);
        ASSERT_EQ(applicationEventSink.SessionCleanupEventCount(), 1u);
        const WorldSessionCleanupApplicationEvent& cleanupEvent = applicationEventSink.SessionCleanupEvent(0);
        EXPECT_EQ(cleanupEvent.kind, WorldSessionCleanupApplicationEventKind::Completed);
        EXPECT_EQ(cleanupEvent.serverTick, ServerTick);
        EXPECT_EQ(cleanupEvent.sessionKey, SessionKey);
        EXPECT_EQ(cleanupEvent.entityKey, entityKey);
    }

    TEST(WorldIngressEventConsumerTests, AppliesOnlyIncreasingControlSequenceToCurrentEntity)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore movementStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, movementStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        WorldEntityComponents components;
        components.playerControl.playerId = 7;
        WorldEntityKey entityKey;
        EntityHandle entityHandle;
        ASSERT_TRUE(entityManager.TryCreate(components, &entityKey, &entityHandle));
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, entityKey));

        std::array<std::byte, protocol::v2::ControlStateCommand::Wire::PayloadBytes> payload;
        ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(
                      protocol::v2::ControlStateCommand{entityKey.generation, 10, protocol::v2::TurnState::Left,
                                                        protocol::v2::BoostState::On},
                      payload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::ControlStateCommand,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));

        ASSERT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::ControlApplied);
        ASSERT_TRUE(entityManager.TryReadComponents(entityHandle, &components));
        EXPECT_EQ(components.playerControl.lastInputSequence, 10u);
        EXPECT_EQ(components.playerControl.turnState, WorldTurnState::Left);
        EXPECT_EQ(components.playerControl.boostState, WorldBoostState::On);

        ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(
                      protocol::v2::ControlStateCommand{entityKey.generation, 10, protocol::v2::TurnState::Right,
                                                        protocol::v2::BoostState::Off},
                      payload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketDropped);

        ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(
                      protocol::v2::ControlStateCommand{entityKey.generation, 9, protocol::v2::TurnState::Right,
                                                        protocol::v2::BoostState::Off},
                      payload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketDropped);
        ASSERT_TRUE(entityManager.TryReadComponents(entityHandle, &components));
        EXPECT_EQ(components.playerControl.lastInputSequence, 10u);
        EXPECT_EQ(components.playerControl.turnState, WorldTurnState::Left);
        EXPECT_EQ(components.playerControl.boostState, WorldBoostState::On);

        ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(
                      protocol::v2::ControlStateCommand{entityKey.generation, 11, protocol::v2::TurnState::Right,
                                                        protocol::v2::BoostState::Off},
                      payload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::ControlApplied);
        ASSERT_TRUE(entityManager.TryReadComponents(entityHandle, &components));
        EXPECT_EQ(components.playerControl.lastInputSequence, 11u);
        EXPECT_EQ(components.playerControl.turnState, WorldTurnState::Right);
        EXPECT_EQ(components.playerControl.boostState, WorldBoostState::Off);
    }

    TEST(WorldIngressEventConsumerTests, RoutesTimeSyncResponseToRuntimeSubmitBoundary)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, EntityKey));

        std::array<std::byte, protocol::v1::WorldTimeSyncRequest::Wire::PayloadBytes> payload;
        constexpr protocol::v1::WorldTimeSyncRequest Request{42};
        ASSERT_EQ(protocol::v1::WorldTimeSyncRequest::Encode(Request, payload), protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::WorldTimeSyncRequest,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));

        // Fake event의 channel과 gateway는 invalid이므로 submit 경계에서 실패한다.
        // TimeSync decode, session admission과 response encode를 모두 통과해야 이 결과에 도달한다.
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::RuntimeSubmitFailed);
    }

    TEST(WorldIngressEventConsumerTests, DoubleBufferedWritesTimeSyncResponseWithoutCallingGateway)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        constexpr WorldEntityKey EntityKey{3, 2};
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 64});
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), ConsumerConfig, 100,
                                           99, applicationEventSink);
        consumer.BeginOutboundTick(100);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, EntityKey));

        std::array<std::byte, protocol::v1::WorldTimeSyncRequest::Wire::PayloadBytes> payload;
        constexpr protocol::v1::WorldTimeSyncRequest Request{42};
        ASSERT_EQ(protocol::v1::WorldTimeSyncRequest::Encode(Request, payload), protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::WorldTimeSyncRequest,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));

        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::TimeSyncSubmitted);
        EXPECT_FALSE(consumer.OutboundBatchFailed());
        EXPECT_EQ(outboundBuffer->WritableUsage().recordCount, 1u);
        EXPECT_EQ(outboundBuffer->WritableUsage().recipientCount, 1u);
    }

    TEST(WorldIngressEventConsumerTests, RecordsInitialAndCadencedAoiReplicationIntoOutboundTicks)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        const WorldIngressEventConsumerConfig AoiConfig{
            WorldJoinConfig{
                20,
                3,
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
            WorldSpatialConfig{1.0f, 10.0f, 12.0f},
            WorldReplicationConfig{3},
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{8, 8, 2048});
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        applicationEventSink.Clear();

        WorldEntityComponents resource;
        resource.transform.positionX = 5.0f;
        resource.replicationMetadata =
            ReplicationMetadataComponent{WorldEntityKind::Resource, 2, WorldShapeKind::Circle, 0.5f};
        WorldEntityKey resourceKey;
        EntityHandle resourceHandle;
        ASSERT_TRUE(entityManager.TryCreate(resource, &resourceKey, &resourceHandle));

        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), AoiConfig, 100, 99,
                                           applicationEventSink);
        ASSERT_TRUE(consumer.AoiReplicationEnabled());
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(100, 100, 100), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(100);
        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        WorldSession pendingSession;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &pendingSession));
        EXPECT_FALSE(pendingSession.IsJoined());
        EXPECT_EQ(entityManager.Size(), 1u);

        std::array<std::byte, 11> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{"Player7"}, joinPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent joinEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest, joinPayload.data(),
                            static_cast<std::uint32_t>(joinPayload.size()));
        ASSERT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::JoinBaselineSubmitted);
        WorldSession joinedSession;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &joinedSession));
        EXPECT_TRUE(joinedSession.IsJoined());
        EXPECT_EQ(joinedSession.displayName, "Player7");
        EXPECT_EQ(entityManager.Size(), 2u);
        ASSERT_EQ(applicationEventSink.JoinEventCount(), 1u);
        const WorldJoinApplicationEvent& committedEvent = applicationEventSink.JoinEvent(0);
        EXPECT_EQ(committedEvent.kind, WorldJoinApplicationEventKind::Committed);
        EXPECT_EQ(committedEvent.failureStage, WorldJoinFailureStage::None);
        EXPECT_EQ(committedEvent.serverTick, 100u);
        EXPECT_EQ(committedEvent.sessionKey, SessionKey);
        EXPECT_EQ(committedEvent.entityKey, joinedSession.entityKey);
        ASSERT_EQ(outboundBuffer->SealWrite(100), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway publishingGateway;
        WorldOutboundPublisher outboundPublisher{*outboundBuffer};
        const WorldOutboundPublishReport initialReport =
            outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0});
        ASSERT_EQ(initialReport.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(initialReport.recordCount, 4u);
        EXPECT_EQ(initialReport.replication.spawnPacketCount, 1u);
        EXPECT_EQ(initialReport.replication.stateBatchPacketCount, 1u);
        EXPECT_EQ(initialReport.replication.gatewayAcceptedCount, 2u);
        EXPECT_EQ(publishingGateway.packetTypes,
                  (std::vector<std::uint16_t>{
                      static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn),
                      static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn),
                      static_cast<std::uint16_t>(protocol::S2CPacketType::WorldReady),
                      static_cast<std::uint16_t>(protocol::S2CPacketType::EntityStateBatch),
                  }));
        protocol::v2::WorldReady decodedReady;
        ASSERT_EQ(protocol::v2::WorldReady::Decode(publishingGateway.payloads[2], &decodedReady),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(decodedReady.channelId, 7u);
        EXPECT_EQ(decodedReady.displayName, "Player7");

        ASSERT_EQ(outboundBuffer->BeginWriteBatch(102, 102, 102), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(102);
        const WorldControlledStatePublishReport publishReport =
            consumer.PublishControlledEntityStates(102, 102, sessionRegistry.JoinedSessions());
        EXPECT_EQ(publishReport.submitted, 1u);
        EXPECT_EQ(publishReport.aoiReplication, WorldAoiReplicationRecordResult::Recorded);
        EXPECT_TRUE(publishReport.snapshotPublished);
        EXPECT_EQ(publishReport.suppressedSnapshotCount, 0u);
        ASSERT_EQ(outboundBuffer->SealWrite(102), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        const WorldOutboundPublishReport snapshotReport =
            outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0});
        ASSERT_EQ(snapshotReport.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(snapshotReport.recordCount, 2u);
        EXPECT_EQ(snapshotReport.replication.stateBatchPacketCount, 1u);
        EXPECT_EQ(snapshotReport.replication.stateRecordCount, 1u);
        EXPECT_EQ(snapshotReport.replication.gatewayAcceptedCount, 1u);
        const std::uint16_t controlledStatePacketType =
            static_cast<std::uint16_t>(protocol::S2CPacketType::ControlledEntityState);
        const std::vector<std::uint16_t>::const_iterator controlledState = std::find(
            publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), controlledStatePacketType);
        ASSERT_NE(controlledState, publishingGateway.packetTypes.end());
        const std::size_t controlledStateIndex =
            static_cast<std::size_t>(controlledState - publishingGateway.packetTypes.begin());
        protocol::v1::ControlledEntityState decodedControlledState;
        EXPECT_EQ(protocol::v1::ControlledEntityState::Decode(publishingGateway.payloads[controlledStateIndex],
                                                              &decodedControlledState),
                  protocol::WorldProtocolError::Success);
    }

    TEST(WorldIngressEventConsumerTests, UsesCatchUpBatchLastTickForJoinAndFinalReplicationReservation)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        const WorldIngressEventConsumerConfig aoiConfig{
            WorldJoinConfig{
                20,
                3,
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
            WorldSpatialConfig{1.0f, 10.0f, 12.0f},
            WorldReplicationConfig{1},
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{16, 32, 4096});
        ASSERT_NE(outboundBuffer, nullptr);
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(1, 100, 102), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), aoiConfig, 100, 99,
                                           applicationEventSink);
        consumer.BeginOutboundTick(102);

        ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(RuntimeSessionKey)),
                  WorldIngressEventHandleResult::SessionRegistered);
        std::array<std::byte, 11> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{"Player7"}, joinPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(consumer.Handle(MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest,
                                                  joinPayload.data(), static_cast<std::uint32_t>(joinPayload.size()))),
                  WorldIngressEventHandleResult::JoinBaselineSubmitted);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(100, 102, sessionRegistry.JoinedSessions()));
        const WorldControlledStatePublishReport snapshotReport =
            consumer.PublishControlledEntityStates(100, 102, sessionRegistry.JoinedSessions());
        ASSERT_TRUE(snapshotReport.snapshotPublished);
        EXPECT_FALSE(consumer.OutboundBatchFailed());
        ASSERT_EQ(outboundBuffer->SealWrite(1), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway publishingGateway;
        WorldOutboundPublisher publisher{*outboundBuffer};
        const WorldOutboundPublishReport publishReport =
            publisher.PublishNext(publishingGateway, std::chrono::milliseconds{0});
        ASSERT_EQ(publishReport.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(publishReport.replication.serverTick, 102u);

        const std::uint16_t spawnPacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn);
        const std::vector<std::uint16_t>::const_iterator spawn =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), spawnPacketType);
        ASSERT_NE(spawn, publishingGateway.packetTypes.end());
        const std::size_t spawnIndex = static_cast<std::size_t>(spawn - publishingGateway.packetTypes.begin());
        protocol::v2::EntitySpawn decodedSpawn;
        ASSERT_EQ(protocol::v2::EntitySpawn::Decode(publishingGateway.payloads[spawnIndex], &decodedSpawn),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(decodedSpawn.baseline.serverTick, 100u);
    }

    TEST(WorldIngressEventConsumerTests, PublishesRemoteWholeSnakeStatesForGameplayAoiRecipients)
    {
        constexpr WorldGrowthConfig PlayerGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldPlayerBodyConfig PlayerBodyConfig{PlayerGrowthConfig, 16};
        const WorldIngressEventConsumerConfig gameplayConfig{
            WorldJoinConfig{
                20,
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
                PlayerBodyConfig,
                7,
            },
            1,
            WorldSpatialConfig{100.0f, 300.0f, 320.0f},
            WorldReplicationConfig{1},
            WorldGameplayConfig{
                1,
                5,
                1200,
                60,
                2,
                0.5f,
                1,
                0.000032f,
                WorldBoostCostConfig{PlayerGrowthConfig, 4.0f},
                1.0f,
                0.25f,
            },
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{64, 64, 65536});
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        applicationEventSink.Clear();
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), gameplayConfig, 100,
                                           99, applicationEventSink);
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(100, 100, 100), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(100);

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);
        constexpr std::array<psnr::core::NrSessionKey, 2> SessionKeys{10, 20};
        for (const psnr::core::NrSessionKey sessionKey : SessionKeys)
        {
            ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(sessionKey)), WorldIngressEventHandleResult::SessionRegistered);
            const FakeToWorldEvent joinEvent =
                MakePacketEvent(sessionKey, protocol::C2SPacketType::JoinWorldRequest, joinPayload.data(),
                                static_cast<std::uint32_t>(joinPayload.size()));
            ASSERT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::JoinBaselineSubmitted);
        }
        ASSERT_EQ(sessionRegistry.JoinedSessions().size(), 2u);
        ASSERT_EQ(outboundBuffer->SealWrite(100), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway publishingGateway;
        WorldOutboundPublisher outboundPublisher{*outboundBuffer};
        ASSERT_EQ(outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::Published);
        const std::uint16_t worldReadyPacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::WorldReady);
        const std::uint16_t initialStatePacketType =
            static_cast<std::uint16_t>(protocol::S2CPacketType::EntityStateBatch);
        const std::vector<std::uint16_t>::const_reverse_iterator lastWorldReady = std::find(
            publishingGateway.packetTypes.rbegin(), publishingGateway.packetTypes.rend(), worldReadyPacketType);
        const std::vector<std::uint16_t>::const_iterator firstInitialState = std::find(
            publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), initialStatePacketType);
        ASSERT_NE(lastWorldReady, publishingGateway.packetTypes.rend());
        ASSERT_NE(firstInitialState, publishingGateway.packetTypes.end());
        const std::size_t lastWorldReadyIndex =
            static_cast<std::size_t>(publishingGateway.packetTypes.rend() - lastWorldReady - 1);
        const std::size_t firstInitialStateIndex =
            static_cast<std::size_t>(firstInitialState - publishingGateway.packetTypes.begin());
        EXPECT_LT(lastWorldReadyIndex, firstInitialStateIndex);
        publishingGateway.packetTypes.clear();
        publishingGateway.payloads.clear();

        ASSERT_EQ(outboundBuffer->BeginWriteBatch(101, 101, 101), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(101);
        const WorldControlledStatePublishReport publishReport =
            consumer.PublishControlledEntityStates(101, 101, sessionRegistry.JoinedSessions());
        ASSERT_TRUE(publishReport.snapshotPublished);
        ASSERT_EQ(publishReport.aoiReplication, WorldAoiReplicationRecordResult::Recorded);
        ASSERT_EQ(outboundBuffer->SealWrite(101), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        const WorldOutboundPublishReport outboundReport =
            outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0});
        ASSERT_EQ(outboundReport.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(outboundReport.replication.stateBatchPacketCount, 2u);
        EXPECT_EQ(outboundReport.replication.stateRecordCount, 2u);

        const std::uint16_t statePacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::EntityStateBatch);
        std::vector<protocol::v2::EntityStateBatch> decodedBatches;
        for (std::size_t packetIndex = 0; packetIndex < publishingGateway.packetTypes.size(); ++packetIndex)
        {
            if (publishingGateway.packetTypes[packetIndex] != statePacketType)
            {
                continue;
            }
            decodedBatches.emplace_back();
            ASSERT_EQ(
                protocol::v2::EntityStateBatch::Decode(publishingGateway.payloads[packetIndex], &decodedBatches.back()),
                protocol::WorldProtocolError::Success);
        }
        ASSERT_EQ(decodedBatches.size(), 2u);
        EXPECT_NE(decodedBatches[0].snapshotId, 0u);
        EXPECT_EQ(decodedBatches[0].snapshotId, decodedBatches[1].snapshotId);
        EXPECT_EQ(decodedBatches[0].serverTick, 101u);
        EXPECT_EQ(decodedBatches[1].serverTick, 101u);
        ASSERT_EQ(decodedBatches[0].records.size(), 1u);
        ASSERT_EQ(decodedBatches[1].records.size(), 1u);
        const WorldEntityKey firstRemoteEntityKey{decodedBatches[0].records[0].entityId,
                                                  decodedBatches[0].records[0].generation};
        const WorldEntityKey secondRemoteEntityKey{decodedBatches[1].records[0].entityId,
                                                   decodedBatches[1].records[0].generation};
        EXPECT_NE(firstRemoteEntityKey, secondRemoteEntityKey);
        EXPECT_FALSE(decodedBatches[0].records[0].bodyTrailSamples.empty());
        EXPECT_FALSE(decodedBatches[1].records[0].bodyTrailSamples.empty());
    }

    TEST(WorldIngressEventConsumerTests, PreservesDurableGameplayOrderBeforeFinalCatchUpSnapshot)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr psnr::core::NrSessionKey ObserverSessionKey = 20;
        constexpr WorldGrowthConfig PlayerGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldPlayerBodyConfig PlayerBodyConfig{PlayerGrowthConfig, 16};
        const WorldIngressEventConsumerConfig gameplayConfig{
            WorldJoinConfig{
                20,
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
                PlayerBodyConfig,
                7,
            },
            1,
            WorldSpatialConfig{100.0f, 300.0f, 320.0f},
            WorldReplicationConfig{1},
            WorldGameplayConfig{
                1,
                5,
                1200,
                60,
                2,
                0.5f,
                1,
                0.000032f,
                WorldBoostCostConfig{PlayerGrowthConfig, 4.0f},
                1.0f,
                0.25f,
            },
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{32, 32, 4096});
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), gameplayConfig, 100,
                                           99, applicationEventSink);
        ASSERT_TRUE(consumer.GameplayEnabled());
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(100, 100, 100), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(100);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        std::array<std::byte, 11> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{"Player7"}, joinPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent joinEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest, joinPayload.data(),
                            static_cast<std::uint32_t>(joinPayload.size()));
        ASSERT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::JoinBaselineSubmitted);

        ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(ObserverSessionKey)),
                  WorldIngressEventHandleResult::SessionRegistered);
        std::array<std::byte, protocol::v1::ObserveWorldRequest::Wire::PayloadBytes> observePayload;
        ASSERT_EQ(protocol::v1::ObserveWorldRequest::Encode(protocol::v1::ObserveWorldRequest{}, observePayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(
            consumer.Handle(MakePacketEvent(ObserverSessionKey, protocol::C2SPacketType::ObserveWorldRequest,
                                            observePayload.data(), static_cast<std::uint32_t>(observePayload.size()))),
            WorldIngressEventHandleResult::ObserverBaselineSubmitted);
        ASSERT_EQ(sessionRegistry.JoinedSessions().size(), 1u);
        ASSERT_EQ(sessionRegistry.RegisteredSessions().size(), 2u);
        WorldSession observerSession;
        ASSERT_TRUE(sessionRegistry.TryFind(WorldSessionKey{ObserverSessionKey}, &observerSession));
        EXPECT_TRUE(observerSession.IsObserver());
        EXPECT_FALSE(observerSession.IsJoined());
        ASSERT_EQ(outboundBuffer->SealWrite(100), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway publishingGateway;
        WorldOutboundPublisher outboundPublisher{*outboundBuffer};
        const WorldOutboundPublishReport joinReport =
            outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0});
        ASSERT_EQ(joinReport.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(publishingGateway.packetTypes, (std::vector<std::uint16_t>{
                                                     static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn),
                                                     static_cast<std::uint16_t>(protocol::S2CPacketType::ScoreState),
                                                     static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState),
                                                     static_cast<std::uint16_t>(protocol::S2CPacketType::WorldReady),
                                                     static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState),
                                                     static_cast<std::uint16_t>(protocol::S2CPacketType::ObserverReady),
                                                 }));
        protocol::v1::ObserverReady observerReady;
        ASSERT_EQ(protocol::v1::ObserverReady::Decode(publishingGateway.payloads.back(), &observerReady),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(observerReady.currentServerTick, 100u);
        EXPECT_EQ(observerReady.tickRateHz, 20u);
        EXPECT_EQ(observerReady.channelId, 7u);

        publishingGateway.packetTypes.clear();
        publishingGateway.payloads.clear();
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(101, 101, 102), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(102);
        ASSERT_EQ(consumer.ProcessGameplayTick(101, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(101, 102, sessionRegistry.JoinedSessions()));
        ASSERT_EQ(consumer.ProcessGameplayTick(102, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(102, 102, sessionRegistry.JoinedSessions()));
        const WorldControlledStatePublishReport publishReport =
            consumer.PublishControlledEntityStates(101, 102, sessionRegistry.JoinedSessions());
        EXPECT_EQ(publishReport.submitted, 1u);
        EXPECT_TRUE(publishReport.snapshotPublished);
        EXPECT_EQ(publishReport.suppressedSnapshotCount, 1u);
        const WorldOverviewPublishReport overviewReport =
            consumer.PublishWorldOverview(100, 102, sessionRegistry.JoinedSessions());
        EXPECT_TRUE(overviewReport.overviewPublished);
        EXPECT_EQ(overviewReport.recipientCount, 2u);
        EXPECT_EQ(overviewReport.chunkCount, 1u);
        EXPECT_EQ(overviewReport.suppressedOverviewCount, 0u);
        const WorldOverviewPublishReport beforeNextOverview =
            consumer.PublishWorldOverview(103, 109, sessionRegistry.JoinedSessions());
        EXPECT_FALSE(beforeNextOverview.overviewPublished);
        ASSERT_EQ(outboundBuffer->SealWrite(101), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        const WorldOutboundPublishReport gameplayReport =
            outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0});

        ASSERT_EQ(gameplayReport.stopReason, WorldOutboundPublishStopReason::Published);
        ASSERT_GE(publishingGateway.packetTypes.size(), 3u);
        const std::uint16_t scoreStatePacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::ScoreState);
        const std::uint16_t roundStatePacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState);
        const std::uint16_t controlledStatePacketType =
            static_cast<std::uint16_t>(protocol::S2CPacketType::ControlledEntityState);
        const std::uint16_t overviewPacketType =
            static_cast<std::uint16_t>(protocol::S2CPacketType::WorldOverviewSnapshot);
        const std::vector<std::uint16_t>::const_iterator scoreState =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), scoreStatePacketType);
        const std::vector<std::uint16_t>::const_iterator roundState =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), roundStatePacketType);
        const std::vector<std::uint16_t>::const_iterator controlledState = std::find(
            publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), controlledStatePacketType);
        const std::vector<std::uint16_t>::const_iterator overview =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), overviewPacketType);
        ASSERT_NE(scoreState, publishingGateway.packetTypes.end());
        ASSERT_NE(roundState, publishingGateway.packetTypes.end());
        ASSERT_NE(controlledState, publishingGateway.packetTypes.end());
        ASSERT_NE(overview, publishingGateway.packetTypes.end());
        EXPECT_LT(scoreState, roundState);
        EXPECT_LT(roundState, controlledState);
        EXPECT_LT(controlledState, overview);
        EXPECT_EQ(std::count(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(),
                             controlledStatePacketType),
                  1);
        EXPECT_EQ(std::count(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(),
                             roundStatePacketType),
                  2);
        EXPECT_EQ(gameplayReport.replication.serverTick, 102u);
        EXPECT_EQ(gameplayReport.replication.spawnPacketCount, 1u);
        EXPECT_EQ(gameplayReport.replication.stateBatchPacketCount, 0u);
        const std::uint16_t spawnPacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn);
        const std::vector<std::uint16_t>::const_iterator spawn =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), spawnPacketType);
        ASSERT_NE(spawn, publishingGateway.packetTypes.end());
        const std::size_t spawnIndex = static_cast<std::size_t>(spawn - publishingGateway.packetTypes.begin());
        protocol::v2::EntitySpawn decodedSpawn;
        ASSERT_EQ(protocol::v2::EntitySpawn::Decode(publishingGateway.payloads[spawnIndex], &decodedSpawn),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(decodedSpawn.baseline.serverTick, 101u);
        const std::size_t controlledStateIndex =
            static_cast<std::size_t>(controlledState - publishingGateway.packetTypes.begin());
        protocol::v2::ControlledEntityState decodedControlledState;
        ASSERT_EQ(protocol::v2::ControlledEntityState::Decode(publishingGateway.payloads[controlledStateIndex],
                                                              &decodedControlledState),
                  protocol::WorldProtocolError::Success);
        WorldSession disconnectingSession;
        ASSERT_TRUE(sessionRegistry.TryFind(WorldSessionKey{RuntimeSessionKey}, &disconnectingSession));
        EXPECT_EQ(decodedControlledState.serverTick, 102u);
        EXPECT_EQ(decodedControlledState.controlledEntityGeneration, disconnectingSession.entityKey.generation);
        EXPECT_EQ(decodedControlledState.lastProcessedControlSequence, 0u);
        EXPECT_EQ(decodedControlledState.growthPoint, 0u);
        EXPECT_FALSE(decodedControlledState.bodyTrailSamples.empty());
        const std::size_t overviewIndex = static_cast<std::size_t>(overview - publishingGateway.packetTypes.begin());
        protocol::v3::WorldOverviewSnapshot decodedOverview;
        ASSERT_EQ(
            protocol::v3::WorldOverviewSnapshot::Decode(publishingGateway.payloads[overviewIndex], &decodedOverview),
            protocol::WorldProtocolError::Success);
        EXPECT_EQ(decodedOverview.serverTick, 102u);
        EXPECT_EQ(decodedOverview.overviewId, 1u);
        EXPECT_EQ(decodedOverview.chunkIndex, 0u);
        EXPECT_EQ(decodedOverview.chunkCount, 1u);
        ASSERT_EQ(decodedOverview.players.size(), 1u);
        EXPECT_EQ(decodedOverview.players[0].playerId, disconnectingSession.playerId);
        EXPECT_FALSE(decodedOverview.players[0].bodySamples.empty());
        ASSERT_EQ(decodedOverview.leaderboard.size(), 1u);
        EXPECT_EQ(decodedOverview.leaderboard[0].playerId, disconnectingSession.playerId);
        EXPECT_EQ(decodedOverview.leaderboard[0].displayName, "Player7");
        constexpr WorldActiveArea StartActiveArea{0.0f, 0.0f, 100.0f, 1.0f, 100.0f};
        EXPECT_TRUE(StartActiveArea.ContainsCircleStrictly(decodedSpawn.baseline.positionX,
                                                           decodedSpawn.baseline.positionY,
                                                           gameplayConfig.gameplay.resourceCircleRadius));
        EXPECT_EQ(consumer.GameplayState().RoundState().phase, WorldRoundPhase::Running);
        ASSERT_EQ(consumer.GameplayState().ResourceSlots().size(), 1u);
        EXPECT_EQ(consumer.GameplayState().ResourceSlots()[0].phase, WorldResourceSlotPhase::Active);

        EntityHandle disconnectingEntityHandle;
        WorldEntityComponents disconnectingComponents;
        ASSERT_TRUE(entityManager.TryFindHandle(disconnectingSession.entityKey, &disconnectingEntityHandle));
        ASSERT_TRUE(entityManager.TryReadComponents(disconnectingEntityHandle, &disconnectingComponents));

        FakeToWorldEvent closedEvent;
        closedEvent.kind = psnr::runtime::NrToWorldEventKind::SessionClosed;
        closedEvent.sessionKey = RuntimeSessionKey;
        closedEvent.endReason = psnr::runtime::NrSessionEndReason::RemoteClosed;
        ASSERT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);

        const std::span<const WorldDisconnectDropSnapshot> snapshots = consumer.PendingDisconnectDropSnapshots();
        ASSERT_EQ(snapshots.size(), 1u);
        EXPECT_EQ(snapshots[0].playerId, disconnectingSession.playerId);
        EXPECT_EQ(snapshots[0].sourceEntityKey, disconnectingSession.entityKey);
        EXPECT_EQ(snapshots[0].growthPoint, 0u);
        EXPECT_EQ(snapshots[0].headTransform, disconnectingComponents.transform);
        EXPECT_EQ(snapshots[0].bodyTrail, disconnectingComponents.bodyTrail);
        EXPECT_FALSE(entityManager.TryReadComponents(disconnectingEntityHandle, &disconnectingComponents));
        EXPECT_EQ(sessionRegistry.Size(), 1u);

        closedEvent.sessionKey = ObserverSessionKey;
        ASSERT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);
        EXPECT_EQ(sessionRegistry.Size(), 0u);

        consumer.BeginOutboundTick(103);
        ASSERT_EQ(consumer.ProcessGameplayTick(103, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        EXPECT_TRUE(consumer.PendingDisconnectDropSnapshots().empty());
    }

    TEST(WorldIngressEventConsumerTests, PublishesRoundResultOnceAfterEndTickCommit)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr psnr::core::NrSessionKey ObserverSessionKey = 30;
        constexpr WorldGrowthConfig PlayerGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldPlayerBodyConfig PlayerBodyConfig{PlayerGrowthConfig, 16};
        const WorldIngressEventConsumerConfig gameplayConfig{
            WorldJoinConfig{
                20,
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
                PlayerBodyConfig,
                7,
            },
            1,
            WorldSpatialConfig{100.0f, 300.0f, 320.0f},
            WorldReplicationConfig{1},
            WorldGameplayConfig{
                1,
                5,
                1,
                60,
                2,
                0.5f,
                1,
                0.000032f,
                WorldBoostCostConfig{PlayerGrowthConfig, 4.0f},
                1.0f,
                0.25f,
            },
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{32, 32, 4096});
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), gameplayConfig, 100,
                                           99, applicationEventSink);
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(100, 100, 100), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(100);

        ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(RuntimeSessionKey)),
                  WorldIngressEventHandleResult::SessionRegistered);
        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(consumer.Handle(MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest,
                                                  joinPayload.data(), static_cast<std::uint32_t>(joinPayload.size()))),
                  WorldIngressEventHandleResult::JoinBaselineSubmitted);
        ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(ObserverSessionKey)),
                  WorldIngressEventHandleResult::SessionRegistered);
        std::array<std::byte, protocol::v1::ObserveWorldRequest::Wire::PayloadBytes> observePayload;
        ASSERT_EQ(protocol::v1::ObserveWorldRequest::Encode(protocol::v1::ObserveWorldRequest{}, observePayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(
            consumer.Handle(MakePacketEvent(ObserverSessionKey, protocol::C2SPacketType::ObserveWorldRequest,
                                            observePayload.data(), static_cast<std::uint32_t>(observePayload.size()))),
            WorldIngressEventHandleResult::ObserverBaselineSubmitted);
        ASSERT_EQ(outboundBuffer->SealWrite(100), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        FakeOutboundGateway gatewaySink;
        WorldOutboundPublisher outboundPublisher{*outboundBuffer};
        ASSERT_EQ(outboundPublisher.PublishNext(gatewaySink, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::Published);

        gatewaySink.packetTypes.clear();
        gatewaySink.payloads.clear();
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(102, 101, 102), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(102);
        ASSERT_EQ(consumer.ProcessGameplayTick(101, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(101, 102, sessionRegistry.JoinedSessions()));
        ASSERT_EQ(consumer.ProcessGameplayTick(102, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(102, 102, sessionRegistry.JoinedSessions()));
        ASSERT_EQ(outboundBuffer->SealWrite(102), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(outboundPublisher.PublishNext(gatewaySink, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::Published);

        const std::uint16_t packetType = static_cast<std::uint16_t>(protocol::S2CPacketType::RoundResult);
        EXPECT_EQ(std::count(gatewaySink.packetTypes.begin(), gatewaySink.packetTypes.end(), packetType), 2);
        for (std::size_t packetIndex = 0; packetIndex < gatewaySink.packetTypes.size(); ++packetIndex)
        {
            if (gatewaySink.packetTypes[packetIndex] != packetType)
            {
                continue;
            }
            protocol::v2::RoundResult roundResult;
            ASSERT_EQ(protocol::v2::RoundResult::Decode(gatewaySink.payloads[packetIndex], &roundResult),
                      protocol::WorldProtocolError::Success);
            EXPECT_EQ(roundResult, (protocol::v2::RoundResult{102, 1, 0, 0, {1}}));
        }
        EXPECT_EQ(consumer.GameplayState().RoundState().phase, WorldRoundPhase::Ended);
        EXPECT_FALSE(consumer.ShouldProcessSimulation());

        WorldSession endedSession;
        ASSERT_TRUE(sessionRegistry.TryFind(WorldSessionKey{RuntimeSessionKey}, &endedSession));
        std::array<std::byte, protocol::v2::ControlStateCommand::Wire::PayloadBytes> controlPayload;
        ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(
                      protocol::v2::ControlStateCommand{endedSession.entityKey.generation, 1,
                                                        protocol::v2::TurnState::Left, protocol::v2::BoostState::On},
                      controlPayload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(
            consumer.Handle(MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::ControlStateCommand,
                                            controlPayload.data(), static_cast<std::uint32_t>(controlPayload.size()))),
            WorldIngressEventHandleResult::PacketRejected);

        constexpr psnr::core::NrSessionKey LateSessionKey = 20;
        ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(LateSessionKey)), WorldIngressEventHandleResult::SessionRegistered);
        EXPECT_EQ(consumer.Handle(MakePacketEvent(LateSessionKey, protocol::C2SPacketType::JoinWorldRequest,
                                                  joinPayload.data(), static_cast<std::uint32_t>(joinPayload.size()))),
                  WorldIngressEventHandleResult::JoinRejected);

        FakeToWorldEvent closedEvent;
        closedEvent.kind = psnr::runtime::NrToWorldEventKind::SessionClosed;
        closedEvent.sessionKey = RuntimeSessionKey;
        closedEvent.endReason = psnr::runtime::NrSessionEndReason::RemoteClosed;
        ASSERT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);

        EXPECT_EQ(sessionRegistry.Size(), 2u);
        EXPECT_EQ(entityManager.Size(), 0u);
        EXPECT_EQ(consumer.GameplayState().PlayerCount(), 0u);
        EXPECT_EQ(consumer.GameplayState().RoundState(), (WorldRoundRuntimeState{2, WorldRoundPhase::Waiting, 0, 0}));
        EXPECT_TRUE(consumer.ShouldProcessSimulation());
        EXPECT_TRUE(consumer.GameplayState().ResourceRegistry().Instances().empty());
        ASSERT_EQ(consumer.GameplayState().ResourceSlots().size(), 1u);
        EXPECT_EQ(consumer.GameplayState().ResourceSlots()[0].phase, WorldResourceSlotPhase::Dormant);

        closedEvent.sessionKey = LateSessionKey;
        ASSERT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);
        EXPECT_EQ(sessionRegistry.Size(), 1u);

        closedEvent.sessionKey = ObserverSessionKey;
        ASSERT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);
        EXPECT_EQ(sessionRegistry.Size(), 0u);

        ASSERT_EQ(outboundBuffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(103, 103, 103), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(103);
        ASSERT_EQ(consumer.ProcessGameplayTick(103, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(103, 103, sessionRegistry.JoinedSessions()));

        constexpr psnr::core::NrSessionKey ReconnectSessionKey = 30;
        ASSERT_EQ(consumer.Handle(MakeAcceptedEvent(ReconnectSessionKey)),
                  WorldIngressEventHandleResult::SessionRegistered);
        EXPECT_EQ(consumer.Handle(MakePacketEvent(ReconnectSessionKey, protocol::C2SPacketType::JoinWorldRequest,
                                                  joinPayload.data(), static_cast<std::uint32_t>(joinPayload.size()))),
                  WorldIngressEventHandleResult::JoinBaselineSubmitted);
    }

    TEST(WorldIngressEventConsumerTests, PublishesControlledDeathBeforeRespawnSpawnAndRebind)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldPlayerBodyConfig PlayerBodyConfig{
            WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f},
            16,
        };
        const WorldIngressEventConsumerConfig gameplayConfig{
            WorldJoinConfig{
                20,
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
                PlayerBodyConfig,
                7,
            },
            1,
            WorldSpatialConfig{1.0f, 10.0f, 12.0f},
            WorldReplicationConfig{1},
            WorldGameplayConfig{
                1,
                5,
                1200,
                60,
                2,
                0.5f,
                1,
                0.000032f,
                WorldBoostCostConfig{PlayerBodyConfig.growth, 1.0f},
            },
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{32, 32, 4096});
        ASSERT_NE(outboundBuffer, nullptr);
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), gameplayConfig, 100,
                                           99, applicationEventSink);
        consumer.BeginOutboundTick(100);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent joinEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest, joinPayload.data(),
                            static_cast<std::uint32_t>(joinPayload.size()));
        ASSERT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::JoinBaselineSubmitted);
        ASSERT_EQ(outboundBuffer->SealWrite(100), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeOutboundGateway publishingGateway;
        WorldOutboundPublisher outboundPublisher{*outboundBuffer};
        ASSERT_EQ(outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::Published);
        publishingGateway.packetTypes.clear();
        publishingGateway.payloads.clear();

        const WorldSession joinedSession = sessionRegistry.JoinedSessions()[0];
        ASSERT_EQ(outboundBuffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(101, 101, 101), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(101);
        ASSERT_EQ(consumer.ProcessGameplayTick(101, WorldPhysicsStepResult{}, sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(101, 101, sessionRegistry.JoinedSessions()));
        ASSERT_EQ(outboundBuffer->SealWrite(101), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::Published);
        publishingGateway.packetTypes.clear();
        publishingGateway.payloads.clear();

        const WorldPlayerSpawnPlannerConfig spawnConfig{
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f}, 4, 1, 5.0f, PlayerBodyConfig,
        };
        WorldResult<WorldPlayerSpawnCandidate> candidateResult =
            WorldPlayerSpawnPlanner::Plan(spawnConfig, 102, joinedSession.playerId, 0);
        ASSERT_TRUE(candidateResult.Succeeded());
        const WorldPlayerSpawnCandidate candidate = candidateResult.TakeValue();
        const std::array<WorldEntityKey, 1> deathSet{joinedSession.entityKey};
        const std::span<const WorldPlayerSpawnCandidate> candidates{&candidate, 1};
        ASSERT_EQ(outboundBuffer->WaitPrepareWrite(std::chrono::milliseconds::zero()),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(outboundBuffer->BeginWriteBatch(102, 102, 102), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        consumer.BeginOutboundTick(102);
        ASSERT_EQ(consumer.ProcessGameplayTick(102, WorldPhysicsStepResult{}, deathSet, candidates,
                                               sessionRegistry.JoinedSessions()),
                  WorldGameplayTickRecordResult::Recorded);
        ASSERT_TRUE(consumer.RecordDurableTickOutbound(102, 102, sessionRegistry.JoinedSessions()));
        ASSERT_EQ(outboundBuffer->SealWrite(102), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(outboundPublisher.PublishNext(publishingGateway, std::chrono::milliseconds{0}).stopReason,
                  WorldOutboundPublishStopReason::Published);

        const std::uint16_t removePacketType = static_cast<std::uint16_t>(protocol::S2CPacketType::EntityRemove);
        const std::uint16_t rebindPacketType =
            static_cast<std::uint16_t>(protocol::S2CPacketType::ControlledEntityRebind);
        const std::vector<std::uint16_t>::const_iterator remove =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), removePacketType);
        const std::vector<std::uint16_t>::const_iterator rebind =
            std::find(publishingGateway.packetTypes.begin(), publishingGateway.packetTypes.end(), rebindPacketType);
        ASSERT_NE(remove, publishingGateway.packetTypes.end());
        ASSERT_NE(rebind, publishingGateway.packetTypes.end());
        const std::size_t removeIndex = static_cast<std::size_t>(remove - publishingGateway.packetTypes.begin());
        const std::size_t rebindIndex = static_cast<std::size_t>(rebind - publishingGateway.packetTypes.begin());
        ASSERT_GT(rebindIndex, 0u);
        EXPECT_LT(removeIndex, rebindIndex - 1);
        EXPECT_EQ(publishingGateway.packetTypes[rebindIndex - 1],
                  static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn));

        protocol::v1::EntityRemove decodedRemove;
        protocol::v2::EntitySpawn decodedSpawn;
        protocol::v1::ControlledEntityRebind decodedRebind;
        ASSERT_EQ(protocol::v1::EntityRemove::Decode(publishingGateway.payloads[removeIndex], &decodedRemove),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(protocol::v2::EntitySpawn::Decode(publishingGateway.payloads[rebindIndex - 1], &decodedSpawn),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(protocol::v1::ControlledEntityRebind::Decode(publishingGateway.payloads[rebindIndex], &decodedRebind),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(decodedRemove.entityId, joinedSession.entityKey.entityId);
        EXPECT_EQ(decodedRemove.generation, joinedSession.entityKey.generation);
        EXPECT_EQ(decodedRemove.reason, protocol::EntityRemoveReason::Destroyed);
        EXPECT_EQ(decodedRebind.playerId, joinedSession.playerId);
        EXPECT_EQ(decodedRebind.previousEntityId, joinedSession.entityKey.entityId);
        EXPECT_EQ(decodedRebind.previousEntityGeneration, joinedSession.entityKey.generation);
        EXPECT_EQ(decodedRebind.controlledEntityId, decodedSpawn.baseline.entityId);
        EXPECT_EQ(decodedRebind.controlledEntityGeneration, decodedSpawn.baseline.generation);
    }

    TEST(WorldIngressEventConsumerTests, RollsBackPreparedJoinWhenRuntimeSubmitFails)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        applicationEventSink.Clear();
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> payload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, payload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));

        // Fake channel과 invalid gateway를 사용하므로 EntitySpawn submit 경계에서 실패한다.
        // Outbound 등록이 완료되지 않았으므로 session은 pending으로 남고 staged entity는 rollback되어야 한다.
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::RuntimeSubmitFailed);
        ASSERT_EQ(applicationEventSink.JoinEventCount(), 1u);
        const WorldJoinApplicationEvent& rollbackEvent = applicationEventSink.JoinEvent(0);
        EXPECT_EQ(rollbackEvent.kind, WorldJoinApplicationEventKind::RolledBack);
        EXPECT_EQ(rollbackEvent.failureStage, WorldJoinFailureStage::EntitySpawnSubmission);
        EXPECT_EQ(rollbackEvent.serverTick, 100u);
        EXPECT_EQ(rollbackEvent.sessionKey, SessionKey);
        EXPECT_TRUE(rollbackEvent.entityKey.IsValid());

        WorldSession session;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &session));
        EXPECT_FALSE(session.IsJoined());
        EXPECT_EQ(session.playerId, 0u);
        EXPECT_FALSE(session.entityKey.IsValid());
        EXPECT_EQ(entityManager.Size(), 0u);
        EXPECT_TRUE(sessionRegistry.JoinedSessions().empty());

        const WorldControlledStatePublishReport beforeCadenceReport =
            consumer.PublishControlledEntityStates(100, 101, sessionRegistry.JoinedSessions());
        EXPECT_EQ(beforeCadenceReport.attempted, 0u);
        EXPECT_EQ(beforeCadenceReport.submitted, 0u);
        EXPECT_EQ(beforeCadenceReport.rejected, 0u);
        EXPECT_FALSE(beforeCadenceReport.snapshotPublished);
        EXPECT_EQ(beforeCadenceReport.suppressedSnapshotCount, 0u);

        // 예정 snapshot tick 102를 포함하지만 마지막 tick 104는 interval에 정확히 맞지 않는다.
        // 중간 상태를 burst로 보내지 않고 최신 tick 104 상태를 한 번만 제출한다.
        const WorldControlledStatePublishReport publishReport =
            consumer.PublishControlledEntityStates(102, 104, sessionRegistry.JoinedSessions());
        EXPECT_EQ(publishReport.attempted, 0u);
        EXPECT_EQ(publishReport.submitted, 0u);
        EXPECT_EQ(publishReport.rejected, 0u);
        EXPECT_TRUE(publishReport.snapshotPublished);
        EXPECT_EQ(publishReport.suppressedSnapshotCount, 0u);

        FakeToWorldEvent closedEvent;
        closedEvent.kind = psnr::runtime::NrToWorldEventKind::SessionClosed;
        closedEvent.sessionKey = RuntimeSessionKey;
        closedEvent.endReason = psnr::runtime::NrSessionEndReason::ApplicationPolicy;

        EXPECT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);
        EXPECT_EQ(sessionRegistry.Size(), 0u);
        EXPECT_EQ(entityManager.Size(), 0u);
    }

    TEST(WorldIngressEventConsumerTests, CommitsJoinOnlyAfterWorldReadyFitsOutboundBatch)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        WorldIngressEventConsumerConfig gameplayConfig = ConsumerConfig;
        gameplayConfig.gameplay = WorldGameplayConfig{
            1, 5, 1200, 60, 2, 0.5f, 1, 0.000032f,
        };
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{3, 3, 2048});
        ASSERT_NE(outboundBuffer, nullptr);

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), gameplayConfig, 100,
                                           99, applicationEventSink);
        ASSERT_TRUE(consumer.GameplayEnabled());
        consumer.BeginOutboundTick(100);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> payload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, payload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent joinEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest,
                                                     payload.data(), static_cast<std::uint32_t>(payload.size()));

        EXPECT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::RuntimeSubmitFailed);
        EXPECT_TRUE(consumer.OutboundBatchFailed());
        EXPECT_EQ(outboundBuffer->WritableUsage().recordCount, 3u);

        WorldSession session;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &session));
        EXPECT_FALSE(session.IsJoined());
        EXPECT_EQ(session.playerId, 0u);
        EXPECT_FALSE(session.entityKey.IsValid());
        EXPECT_TRUE(sessionRegistry.JoinedSessions().empty());
        EXPECT_EQ(entityManager.Size(), 0u);
        EXPECT_EQ(consumer.GameplayState().PlayerCount(), 0u);

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> movementPayload;
        ASSERT_EQ(protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{1, 100, 0, 0}, movementPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent movementEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput, movementPayload.data(),
                            static_cast<std::uint32_t>(movementPayload.size()));
        EXPECT_EQ(consumer.Handle(movementEvent), WorldIngressEventHandleResult::PacketRejected);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldIngressEventConsumerTests, RejectsPacketsOutsideAcceptedSessionLifetime)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);
        FakeToWorldEvent packetEvent = MakePacketEvent(10, protocol::C2SPacketType::MovementInput, nullptr, 0);

        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::SessionNotFound);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldIngressEventConsumerTests, RequestsImmediateProtocolCloseForMalformedPayload)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        applicationEventSink.Clear();
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);
        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);

        const std::array<std::byte, 1> malformedPayload{std::byte{1}};
        FakeToWorldEvent packetEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput, malformedPayload.data(),
                            static_cast<std::uint32_t>(malformedPayload.size()));

        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::ProtocolCloseRequested);
        const WorldIngressMetrics metrics = consumer.Metrics();
        EXPECT_EQ(metrics.malformedPayloadCount, 1u);
        EXPECT_EQ(metrics.protocolCloseRequestCount, 1u);
        EXPECT_EQ(metrics.protocolCloseRequestSuccessCount, 0u);
        EXPECT_EQ(metrics.protocolCloseRequestFailureCount, 1u);
        EXPECT_EQ(metrics.protocolErrorSessionClosedCount, 0u);
        ASSERT_EQ(applicationEventSink.ProtocolCloseEventCount(), 1u);
        const WorldProtocolCloseApplicationEvent& closeEvent = applicationEventSink.ProtocolCloseEvent(0);
        EXPECT_EQ(closeEvent.kind, WorldProtocolCloseApplicationEventKind::RequestFailed);
        EXPECT_EQ(closeEvent.cause, WorldProtocolCloseCause::MalformedPayload);
        EXPECT_EQ(closeEvent.serverTick, 100u);
        EXPECT_EQ(closeEvent.sessionKey, WorldSessionKey{RuntimeSessionKey});
    }

    TEST(WorldIngressEventConsumerTests, ReconnectAdvancesGenerationAndDropsStaleMovementWithoutClose)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{16, 32, 4096});
        ASSERT_NE(outboundBuffer, nullptr);

        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           WorldOutboundMode::DoubleBuffered, outboundBuffer.get(), ConsumerConfig, 100,
                                           99, applicationEventSink);
        consumer.BeginOutboundTick(100);

        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);

        std::array<std::byte, protocol::v2::JoinWorldRequest::Wire::MinimumPayloadBytes> joinPayload;
        ASSERT_EQ(protocol::v2::JoinWorldRequest::Encode(protocol::v2::JoinWorldRequest{}, joinPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent joinEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::JoinWorldRequest, joinPayload.data(),
                            static_cast<std::uint32_t>(joinPayload.size()));
        ASSERT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::JoinBaselineSubmitted);

        WorldSession firstSession;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &firstSession));
        ASSERT_TRUE(firstSession.IsJoined());

        FakeToWorldEvent closedEvent;
        closedEvent.kind = psnr::runtime::NrToWorldEventKind::SessionClosed;
        closedEvent.sessionKey = RuntimeSessionKey;
        closedEvent.endReason = psnr::runtime::NrSessionEndReason::RemoteClosed;
        ASSERT_EQ(consumer.Handle(closedEvent), WorldIngressEventHandleResult::SessionRemoved);

        consumer.BeginOutboundTick(100);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        ASSERT_EQ(consumer.Handle(joinEvent), WorldIngressEventHandleResult::JoinBaselineSubmitted);

        WorldSession replacementSession;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &replacementSession));
        ASSERT_TRUE(replacementSession.IsJoined());
        ASSERT_EQ(replacementSession.entityKey.entityId, firstSession.entityKey.entityId);
        ASSERT_EQ(replacementSession.entityKey.generation, firstSession.entityKey.generation + 1);

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> movementPayload;
        ASSERT_EQ(protocol::v1::MovementInput::Encode(
                      protocol::v1::MovementInput{firstSession.entityKey.generation, 100, 32767, 0}, movementPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent movementEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput, movementPayload.data(),
                            static_cast<std::uint32_t>(movementPayload.size()));
        EXPECT_EQ(consumer.Handle(movementEvent, WorldInboundMode::DoubleBuffered),
                  WorldIngressEventHandleResult::PacketDropped);

        const WorldIngressMetrics metrics = consumer.Metrics();
        EXPECT_EQ(metrics.staleEntityGenerationDropCount, 1u);
        EXPECT_EQ(metrics.protocolCloseRequestCount, 0u);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldIngressEventConsumerTests, DoubleBufferedSameEpochMovementReplacesWithoutProtocolViolation)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);
        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, EntityKey));

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> movementPayload;
        ASSERT_EQ(protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation, 900, 32767, 0},
                                                      movementPayload),
                  protocol::WorldProtocolError::Success);
        FakeToWorldEvent movementEvent =
            MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput, movementPayload.data(),
                            static_cast<std::uint32_t>(movementPayload.size()));
        ASSERT_EQ(consumer.Handle(movementEvent, WorldInboundMode::DoubleBuffered),
                  WorldIngressEventHandleResult::MovementStored);

        ASSERT_EQ(protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation, 901, -32767, 0},
                                                      movementPayload),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(consumer.Handle(movementEvent, WorldInboundMode::DoubleBuffered),
                  WorldIngressEventHandleResult::MovementStored);

        std::vector<WorldMovementCommand> commands;
        ASSERT_TRUE(commandStore.TryTake(100, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_FLOAT_EQ(commands[0].movementInputX, -1.0f);
        EXPECT_EQ(commandStore.Metrics().replacedCommandCount, 1u);
        EXPECT_EQ(consumer.Metrics().duplicateMovementInputViolationCount, 0u);
        EXPECT_EQ(consumer.Metrics().protocolCloseRequestCount, 0u);
    }

    TEST(WorldIngressEventConsumerTests, TracksDropsAndClosesOnThirdRateLimitedViolation)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        applicationEventSink.Clear();
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);
        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, EntityKey));

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> payload;
        ASSERT_EQ(
            protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation, 109, 0, 0}, payload),
            protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));

        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketRejected);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketRejected);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::ProtocolCloseRequested);

        ASSERT_EQ(protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation - 1, 100, 0, 0},
                                                      payload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketDropped);

        ASSERT_EQ(
            protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation, 99, 0, 0}, payload),
            protocol::WorldProtocolError::Success);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketDropped);

        const WorldIngressMetrics metrics = consumer.Metrics();
        EXPECT_EQ(metrics.futureTargetTickViolationCount, 3u);
        EXPECT_EQ(metrics.staleEntityGenerationDropCount, 1u);
        EXPECT_EQ(metrics.lateTargetTickDropCount, 1u);
        EXPECT_EQ(metrics.protocolCloseRequestCount, 1u);
        EXPECT_EQ(metrics.protocolCloseRequestSuccessCount, 0u);
        EXPECT_EQ(metrics.protocolCloseRequestFailureCount, 1u);
        EXPECT_EQ(commandStore.Size(), 0u);
        ASSERT_EQ(applicationEventSink.ProtocolCloseEventCount(), 1u);
        const WorldProtocolCloseApplicationEvent& closeEvent = applicationEventSink.ProtocolCloseEvent(0);
        EXPECT_EQ(closeEvent.kind, WorldProtocolCloseApplicationEventKind::RequestFailed);
        EXPECT_EQ(closeEvent.cause, WorldProtocolCloseCause::RateLimitedViolation);
        EXPECT_EQ(closeEvent.serverTick, 100u);
        EXPECT_EQ(closeEvent.sessionKey, SessionKey);
    }

    TEST(WorldIngressEventConsumerTests, ResetsRateLimitedViolationWindowAfterTenSeconds)
    {
        constexpr psnr::core::NrSessionKey RuntimeSessionKey = 10;
        constexpr WorldSessionKey SessionKey{RuntimeSessionKey};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);
        FakeToWorldEvent acceptedEvent = MakeAcceptedEvent(RuntimeSessionKey);
        ASSERT_EQ(consumer.Handle(acceptedEvent), WorldIngressEventHandleResult::SessionRegistered);
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(SessionKey, 7, EntityKey));

        std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes> payload;
        ASSERT_EQ(
            protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation, 109, 0, 0}, payload),
            protocol::WorldProtocolError::Success);
        FakeToWorldEvent packetEvent = MakePacketEvent(RuntimeSessionKey, protocol::C2SPacketType::MovementInput,
                                                       payload.data(), static_cast<std::uint32_t>(payload.size()));
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketRejected);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketRejected);

        consumer.UpdateTickContext(300, 299);
        ASSERT_EQ(
            protocol::v1::MovementInput::Encode(protocol::v1::MovementInput{EntityKey.generation, 309, 0, 0}, payload),
            protocol::WorldProtocolError::Success);
        EXPECT_EQ(consumer.Handle(packetEvent), WorldIngressEventHandleResult::PacketRejected);

        EXPECT_EQ(consumer.Metrics().futureTargetTickViolationCount, 3u);
        EXPECT_EQ(consumer.Metrics().protocolCloseRequestCount, 0u);
    }

    TEST(WorldIngressEventConsumerTests, AcceptsPublicRuntimeEventTypeAtAdapterBoundary)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        psnr::runtime::NrServer server;
        psnr::runtime::NrGateway gateway;
        WorldIngressEventConsumer consumer(sessionRegistry, entityManager, commandStore, server, gateway,
                                           ConsumerConfig, 100, 99, applicationEventSink);
        psnr::runtime::NrToWorldEvent event;

        EXPECT_EQ(consumer.Handle(event), WorldIngressEventHandleResult::UnsupportedEventKind);
    }
} // namespace psnr::world::tests
