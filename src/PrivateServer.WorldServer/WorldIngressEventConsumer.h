#pragma once

#include "WorldAoiPlanner.h"
#include "WorldActiveAreaSolver.h"
#include "WorldApplicationEventSink.h"
#include "WorldGameplayPhase.h"
#include "WorldGameplayReplicationPlan.h"
#include "WorldGameplayState.h"
#include "WorldIngressPacketRouter.h"
#include "WorldJoinIngress.h"
#include "WorldOutboundDoubleBuffer.h"
#include "WorldPlayerSpawnPlanner.h"
#include "WorldReplicationConfig.h"
#include "WorldReplicationPublisher.h"
#include "WorldSessionRegistry.h"
#include "WorldSpatialIndex.h"

#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include <PrivateServer/NetworkRuntime/NrServer.h>
#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace psnr::world
{
    enum class WorldIngressEventHandleResult : std::uint8_t
    {
        SessionRegistered = 0,
        SessionRemoved,
        JoinBaselineSubmitted,
        ObserverBaselineSubmitted,
        MovementStored,
        ControlApplied,
        TimeSyncSubmitted,
        DuplicateSession,
        SessionNotFound,
        JoinRejected,
        ObserverRejected,
        PacketRejected,
        TimeSyncRejected,
        RuntimeEventReadFailed,
        RuntimeSubmitFailed,
        WorldStateCleanupFailed,
        UnsupportedEventKind,
        PacketDropped,
        ProtocolCloseRequested,
    };

    struct WorldIngressEventConsumerConfig final
    {
        WorldJoinConfig join{};
        std::uint32_t firstPlayerId = 0;
        WorldSpatialConfig spatial{};
        WorldReplicationConfig replication{};
        WorldGameplayConfig gameplay{};
    };

    enum class WorldAoiReplicationRecordResult : std::uint8_t
    {
        Skipped = 0,
        Recorded,
        ProjectionFailed,
        SpatialBuildFailed,
        AoiPlanFailed,
        ReplicationPlanFailed,
        MissingRecipientChannel,
        OutboundRejected,
    };

    struct WorldControlledStatePublishReport final
    {
        std::uint32_t attempted = 0;
        std::uint32_t submitted = 0;
        std::uint32_t rejected = 0;
        WorldAoiReplicationRecordResult aoiReplication = WorldAoiReplicationRecordResult::Skipped;
        std::uint32_t suppressedSnapshotCount = 0;
        bool snapshotPublished = false;
    };

    struct WorldOverviewPublishReport final
    {
        std::uint32_t recipientCount = 0;
        std::uint32_t chunkCount = 0;
        std::uint32_t suppressedOverviewCount = 0;
        bool overviewPublished = false;
    };

    enum class WorldGameplayTickRecordResult : std::uint8_t
    {
        Skipped = 0,
        Recorded,
        InitializationFailed,
        ComputeFailed,
        CommitFailed,
        BodyFinalizeFailed,
        AoiPruneFailed,
        ReplicationPlanFailed,
        OutboundRejected,
    };

    enum class WorldActiveAreaResolveResult : std::uint8_t
    {
        Inactive = 0,
        Resolved,
        InvalidArgument,
        InvalidState,
        SolveFailed,
    };

    struct WorldIngressMetrics final
    {
        std::uint64_t malformedPayloadCount = 0;
        std::uint64_t lateTargetTickDropCount = 0;
        std::uint64_t staleEntityGenerationDropCount = 0;
        std::uint64_t duplicateMovementInputViolationCount = 0;
        std::uint64_t futureTargetTickViolationCount = 0;
        std::uint64_t protocolCloseRequestCount = 0;
        std::uint64_t protocolCloseRequestSuccessCount = 0;
        std::uint64_t protocolCloseRequestFailureCount = 0;
        std::uint64_t protocolErrorSessionClosedCount = 0;
    };

    // Runtime event lifetime과 World-owned ingress 사이를 번역하는 integration adapter다.
    // Runtime payload view와 send channel은 이 adapter 밖의 domain/command interface로 전달하지 않는다.
    class WorldIngressEventConsumer final
    {
    public:
        WorldIngressEventConsumer(WorldSessionRegistry& sessionRegistry, WorldEntityManager& entityManager,
                                  WorldMovementCommandStore& movementCommandStore, psnr::runtime::NrServer& server,
                                  psnr::runtime::NrGateway& gateway, const WorldIngressEventConsumerConfig& config,
                                  std::uint32_t currentServerTick, std::uint32_t lastCompletedServerTick,
                                  IWorldApplicationEventSink& applicationEventSink) noexcept;
        WorldIngressEventConsumer(WorldSessionRegistry& sessionRegistry, WorldEntityManager& entityManager,
                                  WorldMovementCommandStore& movementCommandStore, psnr::runtime::NrServer& server,
                                  psnr::runtime::NrGateway& gateway, WorldOutboundMode outboundMode,
                                  WorldOutboundDoubleBuffer* outboundBuffer,
                                  const WorldIngressEventConsumerConfig& config, std::uint32_t currentServerTick,
                                  std::uint32_t lastCompletedServerTick,
                                  IWorldApplicationEventSink& applicationEventSink) noexcept;

        void UpdateTickContext(std::uint32_t currentServerTick, std::uint32_t lastCompletedServerTick) noexcept;
        void BeginOutboundTick(std::uint32_t batchLastServerTick) noexcept;
        [[nodiscard]] bool OutboundBatchFailed() const noexcept;
        [[nodiscard]] bool RecordDurableTickOutbound(std::uint32_t serverTick, std::uint32_t batchLastServerTick,
                                                     std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldControlledStatePublishReport PublishControlledEntityStates(
            std::uint32_t firstProcessedServerTick, std::uint32_t lastProcessedServerTick,
            std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldOverviewPublishReport PublishWorldOverview(
            std::uint32_t firstProcessedServerTick, std::uint32_t lastProcessedServerTick,
            std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldGameplayTickRecordResult ProcessGameplayTick(
            std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
            std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldGameplayTickRecordResult ProcessGameplayTick(
            std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
            std::span<const WorldEntityKey> collisionDeathSet, std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldGameplayTickRecordResult ProcessGameplayTick(
            std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
            std::span<const WorldEntityKey> collisionDeathSet,
            std::span<const WorldPlayerSpawnCandidate> playerSpawnCandidates,
            std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldGameplayTickRecordResult ProcessGameplayTick(
            std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
            std::span<const WorldEntityKey> collisionDeathSet,
            std::span<const WorldEntityKey> activeAreaBoundaryDeathSet,
            std::span<const WorldPlayerSpawnCandidate> playerSpawnCandidates,
            std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldGameplayTickRecordResult ProcessGameplayTick(
            std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
            std::span<const WorldEntityKey> collisionDeathSet,
            std::span<const WorldEntityKey> activeAreaBoundaryDeathSet, const WorldActiveArea* activeArea,
            std::span<const WorldPlayerSpawnCandidate> playerSpawnCandidates,
            std::span<const WorldSession> joinedSessions) noexcept;
        [[nodiscard]] WorldActiveAreaResolveResult ResolveActiveArea(std::uint32_t serverTick,
                                                                     WorldActiveArea* outActiveArea) const noexcept;
        [[nodiscard]] bool GameplayEnabled() const noexcept;
        [[nodiscard]] bool ShouldProcessSimulation() const noexcept;
        [[nodiscard]] const WorldGameplayState& GameplayState() const noexcept;

        template <typename TEvent>
        WorldIngressEventHandleResult Handle(const TEvent& event, const WorldInboundMode inboundMode) noexcept
        {
            const WorldSessionKey sessionKey{event.SessionKey()};
            switch (event.Kind())
            {
            case psnr::runtime::NrToWorldEventKind::SessionAccepted:
            {
                psnr::runtime::NrSessionSendChannel sendChannel;
                if (event.GetSendChannel(&sendChannel).Failed())
                {
                    return WorldIngressEventHandleResult::RuntimeEventReadFailed;
                }
                return HandleSessionAccepted(sessionKey, sendChannel);
            }
            case psnr::runtime::NrToWorldEventKind::PacketReceived:
            {
                psnr::core::NrPacketType packetType{};
                psnr::runtime::NrByteView payload;
                if (event.GetPacketType(&packetType).Failed() || event.GetPayload(&payload).Failed())
                {
                    return WorldIngressEventHandleResult::RuntimeEventReadFailed;
                }
                return HandlePacket(sessionKey, packetType, payload, inboundMode);
            }
            case psnr::runtime::NrToWorldEventKind::SessionClosed:
            {
                psnr::runtime::NrSessionEndReason endReason = psnr::runtime::NrSessionEndReason::None;
                if (event.GetEndReason(endReason).Failed() || endReason == psnr::runtime::NrSessionEndReason::None)
                {
                    return WorldIngressEventHandleResult::RuntimeEventReadFailed;
                }
                return HandleSessionClosed(sessionKey, endReason);
            }
            case psnr::runtime::NrToWorldEventKind::None:
                return WorldIngressEventHandleResult::UnsupportedEventKind;
            }

            return WorldIngressEventHandleResult::UnsupportedEventKind;
        }

        template <typename TEvent> WorldIngressEventHandleResult Handle(const TEvent& event) noexcept
        {
            return Handle(event, WorldInboundMode::TargetServerTick);
        }

        [[nodiscard]] std::size_t SessionChannelCount() const noexcept;
        [[nodiscard]] bool AoiReplicationEnabled() const noexcept;
        [[nodiscard]] WorldIngressMetrics Metrics() const noexcept;
        [[nodiscard]] std::span<const WorldDisconnectDropSnapshot> PendingDisconnectDropSnapshots() const noexcept;

    private:
        enum class AoiReplicationContent : std::uint8_t
        {
            LifecycleOnly = 0,
            FullSnapshot,
        };

        struct ProtocolViolationWindow final
        {
            std::uint32_t firstViolationServerTick = 0;
            std::uint32_t violationCount = 0;
            bool closeRequested = false;
        };

        using SessionKeyToSendChannelMap =
            std::unordered_map<WorldSessionKey, psnr::runtime::NrSessionSendChannel, WorldSessionKeyHash>;
        using SessionKeyToProtocolViolationWindowMap =
            std::unordered_map<WorldSessionKey, ProtocolViolationWindow, WorldSessionKeyHash>;

        // !TODO: consumer 내부 handler 함수 처리를 인터페이스로 숨기기
        [[nodiscard]] WorldIngressEventHandleResult HandleSessionAccepted(
            WorldSessionKey sessionKey, const psnr::runtime::NrSessionSendChannel& sendChannel);
        [[nodiscard]] WorldIngressEventHandleResult HandleSessionClosed(WorldSessionKey sessionKey,
                                                                        psnr::runtime::NrSessionEndReason endReason);
        [[nodiscard]] bool ResetEndedRoundIfEmpty() noexcept;
        [[nodiscard]] WorldIngressEventHandleResult HandlePacket(WorldSessionKey sessionKey,
                                                                 psnr::core::NrPacketType packetType,
                                                                 psnr::runtime::NrByteView payload,
                                                                 WorldInboundMode inboundMode) noexcept;
        [[nodiscard]] WorldIngressEventHandleResult HandleJoin(WorldSessionKey sessionKey,
                                                               psnr::runtime::NrByteView payload);
        [[nodiscard]] WorldIngressEventHandleResult HandleObserve(WorldSessionKey sessionKey,
                                                                  psnr::runtime::NrByteView payload) noexcept;
        [[nodiscard]] bool RollbackPreparedJoin(WorldSessionKey sessionKey, std::uint32_t playerId,
                                                const WorldJoinBaseline& baseline, bool gameplayPlayerRegistered,
                                                WorldJoinFailureStage failureStage) noexcept;
        [[nodiscard]] WorldIngressEventHandleResult HandleTimeSync(WorldSessionKey sessionKey,
                                                                   psnr::runtime::NrByteView payload) noexcept;
        [[nodiscard]] WorldIngressEventHandleResult ApplyControlCommand(const WorldControlCommand& command) noexcept;
        [[nodiscard]] WorldIngressEventHandleResult HandleRouteResult(
            WorldSessionKey sessionKey, WorldIngressPacketRouteResult routeResult,
            const WorldControlCommand& controlCommand) noexcept;
        [[nodiscard]] WorldIngressEventHandleResult RequestProtocolClose(WorldSessionKey sessionKey,
                                                                         WorldProtocolCloseCause cause) noexcept;
        [[nodiscard]] bool RecordRateLimitedViolation(WorldSessionKey sessionKey) noexcept;
        [[nodiscard]] psnr::core::NrStatus SubmitOutbound(const psnr::runtime::NrSessionSendChannel& channel,
                                                          psnr::core::NrPacketType packetType,
                                                          psnr::runtime::NrByteView payload) noexcept;
        [[nodiscard]] psnr::core::NrStatus SubmitOutboundMany(
            std::span<const psnr::runtime::NrSessionSendChannel> channels, psnr::core::NrPacketType packetType,
            std::span<const std::byte> payload) noexcept;
        [[nodiscard]] WorldAoiReplicationRecordResult RecordAoiReplication(std::uint32_t serverTick,
                                                                           std::uint32_t replicationBatchServerTick,
                                                                           std::span<const WorldSession> joinedSessions,
                                                                           bool skipInitialRecipients,
                                                                           AoiReplicationContent content) noexcept;
        [[nodiscard]] bool RecordGameplayEntityRemovals(const WorldGameplayReplicationPlan& plan) noexcept;
        [[nodiscard]] bool FinalizePlayerBodies();
        [[nodiscard]] bool FlushPlayerSpawnPublications(std::uint32_t serverTick) noexcept;
        [[nodiscard]] bool RecordRoundResults(const WorldGameplayReplicationPlan& plan) noexcept;
        [[nodiscard]] bool FlushGameplayBroadcast() noexcept;
        [[nodiscard]] bool RecordJoinGameplayBaseline(const psnr::runtime::NrSessionSendChannel& channel) noexcept;
        [[nodiscard]] bool RecordObserverGameplayBaseline(const psnr::runtime::NrSessionSendChannel& channel) noexcept;

        WorldSessionRegistry& sessionRegistry_;
        WorldEntityManager& entityManager_;
        WorldMovementCommandStore& movementCommandStore_;
        psnr::runtime::NrServer& server_;
        psnr::runtime::NrGateway& gateway_;
        IWorldApplicationEventSink& applicationEventSink_; // Must outlive this consumer.
        WorldOutboundMode outboundMode_ = WorldOutboundMode::Direct;
        WorldOutboundDoubleBuffer* outboundBuffer_ = nullptr;
        WorldIngressEventConsumerConfig config_;
        SessionKeyToSendChannelMap sessionKeyToSendChannel_;
        SessionKeyToProtocolViolationWindowMap sessionKeyToProtocolViolationWindow_;
        std::unique_ptr<WorldSpatialIndex> spatialIndex_;
        WorldAoiPlanner aoiPlanner_;
        WorldReplicationPlanner replicationPlanner_;
        WorldReplicationPublisher replicationPublisher_;
        WorldGameplayState gameplayState_;
        WorldGameplayReplicationPlanner gameplayReplicationPlanner_;
        WorldOverviewCadence overviewCadence_;
        WorldOverviewPlanner overviewPlanner_;
        WorldGameplayReplicationPlan pendingGameplayBroadcast_;
        std::vector<WorldGameplayPlayerSpawn> pendingPlayerSpawns_;
        std::vector<WorldDisconnectDropSnapshot> pendingDisconnectDropSnapshots_;
        std::vector<WorldSessionKey> initialAoiRecipientsThisTick_; // 현재 tick 에서 초기 AOI Baseline 을 받은 session
        WorldIngressMetrics metrics_;
        std::uint64_t nextPlayerId_ = 0;
        std::uint64_t nextSnapshotTick_ = 0;
        std::uint64_t nextRemoteEntitySnapshotId_ = 1;
        WorldActiveArea latestOverviewActiveArea_{};
        std::uint32_t currentServerTick_ = 0;
        std::uint32_t lastCompletedServerTick_ = 0;
        std::uint32_t outboundBatchLastServerTick_ = 0; // 같은 slot의 replication 예약이 공유하는 대표 tick
        bool outboundBatchFailed_ = false;
        bool gameplayEnabled_ = false;
        bool gameplayInitializationFailed_ = false;
        bool hasPendingGameplayBroadcast_ = false;
        bool hasLatestOverviewActiveArea_ = false;
    };
} // namespace psnr::world
