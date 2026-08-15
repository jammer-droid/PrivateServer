#include "pch.h"

#include "WorldIngressEventConsumer.h"

#include "ControlledEntityRebind.h"
#include "ControlledEntityState.h"
#include "EntitySpawn.h"
#include "ObserverReady.h"
#include "ObserveWorldRequest.h"
#include "RoundState.h"
#include "ScoreState.h"
#include "WorldGameplayCommitter.h"
#include "WorldPacketTypes.h"
#include "WorldReadView.h"
#include "WorldReady.h"
#include "WorldSpatialProjectionBuilder.h"
#include "WorldTimeSyncIngress.h"
#include "WorldTimeSyncResponse.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace psnr::world
{
    namespace
    {
        struct PreparedPlayerBodyUpdate final
        {
            EntityHandle entityHandle{};
            WorldEntityComponents components{};
        };
    } // namespace

    WorldIngressEventConsumer::WorldIngressEventConsumer(
        WorldSessionRegistry& sessionRegistry, WorldEntityManager& entityManager,
        WorldMovementCommandStore& movementCommandStore, psnr::runtime::NrServer& server,
        psnr::runtime::NrGateway& gateway, const WorldIngressEventConsumerConfig& config,
        const std::uint32_t currentServerTick, const std::uint32_t lastCompletedServerTick,
        IWorldApplicationEventSink& applicationEventSink) noexcept
        : WorldIngressEventConsumer(sessionRegistry, entityManager, movementCommandStore, server, gateway,
                                    WorldOutboundMode::Direct, nullptr, config, currentServerTick,
                                    lastCompletedServerTick, applicationEventSink)
    {
    }

    WorldIngressEventConsumer::WorldIngressEventConsumer(
        WorldSessionRegistry& sessionRegistry, WorldEntityManager& entityManager,
        WorldMovementCommandStore& movementCommandStore, psnr::runtime::NrServer& server,
        psnr::runtime::NrGateway& gateway, const WorldOutboundMode outboundMode,
        WorldOutboundDoubleBuffer* const outboundBuffer, const WorldIngressEventConsumerConfig& config,
        const std::uint32_t currentServerTick, const std::uint32_t lastCompletedServerTick,
        IWorldApplicationEventSink& applicationEventSink) noexcept
        : sessionRegistry_(sessionRegistry)
        , entityManager_(entityManager)
        , movementCommandStore_(movementCommandStore)
        , server_(server)
        , gateway_(gateway)
        , applicationEventSink_(applicationEventSink)
        , outboundMode_(outboundMode)
        , outboundBuffer_(outboundBuffer)
        , config_(config)
        , nextPlayerId_(config.firstPlayerId)
        , nextSnapshotTick_(
              config.join.snapshotIntervalTicks == 0
                  ? 0
                  : static_cast<std::uint64_t>(currentServerTick) +
                        (config.join.snapshotIntervalTicks - currentServerTick % config.join.snapshotIntervalTicks) %
                            config.join.snapshotIntervalTicks)
        , currentServerTick_(currentServerTick)
        , lastCompletedServerTick_(lastCompletedServerTick)
    {
        if (outboundMode_ == WorldOutboundMode::DoubleBuffered && outboundBuffer_ != nullptr &&
            IsValid(config_.spatial) && config_.replication.IsValid() &&
            config_.replication.snapshotIntervalTicks == config_.join.snapshotIntervalTicks)
        {
            WorldResult<std::unique_ptr<WorldSpatialIndex>> spatialIndexResult =
                WorldSpatialIndex::Create(config_.spatial);
            if (spatialIndexResult.Succeeded())
            {
                spatialIndex_ = spatialIndexResult.TakeValue();
            }
        }

        if (config_.gameplay.minimumPlayersToStart != 0)
        {
            const WorldPhysicsArenaBounds arenaBounds{
                config_.join.arenaMinX,
                config_.join.arenaMinY,
                config_.join.arenaMaxX,
                config_.join.arenaMaxY,
            };
            WorldResult<WorldGameplayState> gameplayStateResult =
                CreateWorldGameplayState(config_.gameplay, arenaBounds);
            gameplayEnabled_ = gameplayStateResult.Succeeded();
            if (gameplayEnabled_)
            {
                gameplayState_ = gameplayStateResult.TakeValue();
            }
            gameplayInitializationFailed_ = !gameplayEnabled_;
            if (gameplayEnabled_ && WorldPlayerBody::IsEnabled(config_.join.playerBody) &&
                config_.join.playerBody.growth != config_.gameplay.boostCost.growth)
            {
                gameplayEnabled_ = false;
                gameplayInitializationFailed_ = true;
            }
            if (gameplayEnabled_)
            {
                WorldResult<WorldOverviewCadence> overviewCadenceResult =
                    CreateWorldOverviewCadence(config_.join.tickRateHz, currentServerTick);
                if (overviewCadenceResult.Failed())
                {
                    gameplayEnabled_ = false;
                    gameplayInitializationFailed_ = true;
                }
                else
                {
                    overviewCadence_ = overviewCadenceResult.TakeValue();
                }
            }
        }
    }

    void WorldIngressEventConsumer::UpdateTickContext(const std::uint32_t currentServerTick,
                                                      const std::uint32_t lastCompletedServerTick) noexcept
    {
        currentServerTick_ = currentServerTick;
        lastCompletedServerTick_ = lastCompletedServerTick;
    }

    void WorldIngressEventConsumer::BeginOutboundTick(const std::uint32_t batchLastServerTick) noexcept
    {
        outboundBatchLastServerTick_ = batchLastServerTick;
        outboundBatchFailed_ = false;
        initialAoiRecipientsThisTick_.clear();
    }

    bool WorldIngressEventConsumer::GameplayEnabled() const noexcept
    {
        return gameplayEnabled_;
    }

    bool WorldIngressEventConsumer::ShouldProcessSimulation() const noexcept
    {
        return !gameplayEnabled_ || gameplayState_.RoundState().phase != WorldRoundPhase::Ended;
    }

    const WorldGameplayState& WorldIngressEventConsumer::GameplayState() const noexcept
    {
        return gameplayState_;
    }

    std::span<const WorldDisconnectDropSnapshot> WorldIngressEventConsumer::PendingDisconnectDropSnapshots()
        const noexcept
    {
        return pendingDisconnectDropSnapshots_;
    }

    WorldActiveAreaResolveResult WorldIngressEventConsumer::ResolveActiveArea(
        const std::uint32_t serverTick, WorldActiveArea* const outActiveArea) const noexcept
    {
        if (outActiveArea == nullptr)
        {
            return WorldActiveAreaResolveResult::InvalidArgument;
        }
        if (gameplayInitializationFailed_)
        {
            return WorldActiveAreaResolveResult::InvalidState;
        }
        if (!gameplayEnabled_)
        {
            return WorldActiveAreaResolveResult::Inactive;
        }

        const WorldRoundRuntimeState& roundState = gameplayState_.RoundState();
        if (roundState.phase == WorldRoundPhase::Waiting || roundState.phase == WorldRoundPhase::Ended)
        {
            return WorldActiveAreaResolveResult::Inactive;
        }
        if (roundState.phase != WorldRoundPhase::Running || roundState.roundId == 0 || roundState.winnerPlayerId != 0 ||
            roundState.phaseEndsAtServerTick < config_.gameplay.roundDurationTicks)
        {
            return WorldActiveAreaResolveResult::InvalidState;
        }

        const WorldActiveAreaConfig activeAreaConfig{
            WorldPhysicsArenaBounds{
                config_.join.arenaMinX,
                config_.join.arenaMinY,
                config_.join.arenaMaxX,
                config_.join.arenaMaxY,
            },
            config_.gameplay.roundDurationTicks,
            config_.gameplay.activeAreaStartRatio,
            config_.gameplay.activeAreaEndRatio,
        };
        const std::uint32_t roundStartTick = roundState.phaseEndsAtServerTick - config_.gameplay.roundDurationTicks;
        if (serverTick < roundStartTick || serverTick > roundState.phaseEndsAtServerTick)
        {
            return WorldActiveAreaResolveResult::InvalidState;
        }
        WorldResult<WorldActiveArea> activeAreaResult =
            WorldActiveAreaSolver::Solve(activeAreaConfig, roundStartTick, serverTick);
        if (activeAreaResult.Failed())
        {
            return WorldActiveAreaResolveResult::SolveFailed;
        }
        *outActiveArea = activeAreaResult.TakeValue();
        return WorldActiveAreaResolveResult::Resolved;
    }

    WorldGameplayTickRecordResult WorldIngressEventConsumer::ProcessGameplayTick(
        const std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
        const std::span<const WorldSession> joinedSessions) noexcept
    {
        return ProcessGameplayTick(serverTick, physicsResult, {}, joinedSessions);
    }

    WorldGameplayTickRecordResult WorldIngressEventConsumer::ProcessGameplayTick(
        const std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
        const std::span<const WorldEntityKey> collisionDeathSet,
        const std::span<const WorldSession> joinedSessions) noexcept
    {
        return ProcessGameplayTick(serverTick, physicsResult, collisionDeathSet, {}, joinedSessions);
    }

    WorldGameplayTickRecordResult WorldIngressEventConsumer::ProcessGameplayTick(
        const std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
        const std::span<const WorldEntityKey> collisionDeathSet,
        const std::span<const WorldPlayerSpawnCandidate> playerSpawnCandidates,
        const std::span<const WorldSession>) noexcept
    {
        return ProcessGameplayTick(serverTick, physicsResult, collisionDeathSet, {}, playerSpawnCandidates, {});
    }

    WorldGameplayTickRecordResult WorldIngressEventConsumer::ProcessGameplayTick(
        const std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
        const std::span<const WorldEntityKey> collisionDeathSet,
        const std::span<const WorldEntityKey> activeAreaBoundaryDeathSet,
        const std::span<const WorldPlayerSpawnCandidate> playerSpawnCandidates,
        const std::span<const WorldSession> joinedSessions) noexcept
    {
        WorldActiveArea activeArea;
        const WorldActiveArea* activeAreaPointer = nullptr;
        const WorldActiveAreaResolveResult activeAreaResult = ResolveActiveArea(serverTick, &activeArea);
        if (activeAreaResult == WorldActiveAreaResolveResult::Resolved)
        {
            activeAreaPointer = &activeArea;
        }
        else if (activeAreaResult != WorldActiveAreaResolveResult::Inactive)
        {
            return WorldGameplayTickRecordResult::ComputeFailed;
        }
        return ProcessGameplayTick(serverTick, physicsResult, collisionDeathSet, activeAreaBoundaryDeathSet,
                                   activeAreaPointer, playerSpawnCandidates, joinedSessions);
    }

    WorldGameplayTickRecordResult WorldIngressEventConsumer::ProcessGameplayTick(
        const std::uint32_t serverTick, const WorldPhysicsStepResult& physicsResult,
        const std::span<const WorldEntityKey> collisionDeathSet,
        const std::span<const WorldEntityKey> activeAreaBoundaryDeathSet, const WorldActiveArea* const activeArea,
        const std::span<const WorldPlayerSpawnCandidate> playerSpawnCandidates,
        const std::span<const WorldSession>) noexcept
    {
        if (gameplayInitializationFailed_)
        {
            return WorldGameplayTickRecordResult::InitializationFailed;
        }
        if (!gameplayEnabled_)
        {
            return WorldGameplayTickRecordResult::Skipped;
        }
        if (hasPendingGameplayBroadcast_ || !pendingPlayerSpawns_.empty())
        {
            return WorldGameplayTickRecordResult::OutboundRejected;
        }

        WorldActiveArea spawnActiveArea;
        const WorldActiveArea* phaseActiveArea = activeArea;
        const WorldRoundPhase roundPhase = gameplayState_.RoundState().phase;
        if (phaseActiveArea == nullptr &&
            (roundPhase == WorldRoundPhase::Waiting || roundPhase == WorldRoundPhase::Ended))
        {
            const WorldActiveAreaConfig spawnAreaConfig{
                WorldPhysicsArenaBounds{
                    config_.join.arenaMinX,
                    config_.join.arenaMinY,
                    config_.join.arenaMaxX,
                    config_.join.arenaMaxY,
                },
                config_.gameplay.roundDurationTicks,
                config_.gameplay.activeAreaStartRatio,
                config_.gameplay.activeAreaEndRatio,
            };
            WorldResult<WorldActiveArea> spawnActiveAreaResult =
                WorldActiveAreaSolver::Solve(spawnAreaConfig, serverTick, serverTick);
            if (spawnActiveAreaResult.Failed())
            {
                return WorldGameplayTickRecordResult::ComputeFailed;
            }
            spawnActiveArea = spawnActiveAreaResult.TakeValue();
            phaseActiveArea = &spawnActiveArea;
        }

        try
        {
            const WorldReadView readView{entityManager_};
            const float fixedDeltaSeconds = 1.0f / static_cast<float>(config_.join.tickRateHz);
            WorldResult<WorldGameplayPhaseResult> phaseResultResult = WorldGameplayPhase::Compute(
                serverTick, config_.gameplay, physicsResult, readView, gameplayState_.RoundState(),
                gameplayState_.PlayerScores(), gameplayState_.ResourceSlots(), gameplayState_.ResourceRegistry(),
                collisionDeathSet, activeAreaBoundaryDeathSet, pendingDisconnectDropSnapshots_, fixedDeltaSeconds,
                phaseActiveArea);
            if (phaseResultResult.Failed())
            {
                return WorldGameplayTickRecordResult::ComputeFailed;
            }
            WorldGameplayPhaseResult phaseResult = phaseResultResult.TakeValue();

            WorldGameplayCommitReport commitReport;
            if (WorldGameplayCommitter::Commit(config_.gameplay, phaseResult, entityManager_, gameplayState_,
                                               &commitReport) != WorldGameplayCommitResult::Committed)
            {
                return WorldGameplayTickRecordResult::CommitFailed;
            }
            pendingDisconnectDropSnapshots_.clear();
            const std::span<const WorldSession> joinedSessionsBeforePlayerSpawns = sessionRegistry_.JoinedSessions();
            const std::vector<WorldSession> removalRecipientSessions(joinedSessionsBeforePlayerSpawns.begin(),
                                                                     joinedSessionsBeforePlayerSpawns.end());
            if (WorldGameplayCommitter::CommitPlayerSpawns(playerSpawnCandidates, entityManager_, gameplayState_,
                                                           sessionRegistry_,
                                                           &commitReport) != WorldGameplayCommitResult::Committed)
            {
                return WorldGameplayTickRecordResult::CommitFailed;
            }
            pendingPlayerSpawns_ = commitReport.playerSpawns;
            if (!FinalizePlayerBodies())
            {
                return WorldGameplayTickRecordResult::BodyFinalizeFailed;
            }

            std::vector<WorldEntityKey> removedEntityKeys;
            removedEntityKeys.reserve(commitReport.entityRemovals.size());
            for (const WorldGameplayEntityRemoval& removal : commitReport.entityRemovals)
            {
                removedEntityKeys.push_back(removal.entityKey);
            }
            std::sort(removedEntityKeys.begin(), removedEntityKeys.end());
            removedEntityKeys.erase(std::unique(removedEntityKeys.begin(), removedEntityKeys.end()),
                                    removedEntityKeys.end());

            std::vector<WorldAoiPrunedVisibility> prunedVisibilities;
            if (aoiPlanner_.PruneVisibleEntities(removedEntityKeys, &prunedVisibilities) != WorldAoiPlanResult::Planned)
            {
                return WorldGameplayTickRecordResult::AoiPruneFailed;
            }

            const std::span<const WorldSession> committedJoinedSessions = sessionRegistry_.JoinedSessions();
            WorldResult<WorldGameplayReplicationPlan> removalPlanResult =
                gameplayReplicationPlanner_.BuildEntityRemovals(commitReport, prunedVisibilities,
                                                                removalRecipientSessions);
            if (removalPlanResult.Failed())
            {
                return WorldGameplayTickRecordResult::ReplicationPlanFailed;
            }
            WorldGameplayReplicationPlan removalPlan = removalPlanResult.TakeValue();
            if (!RecordGameplayEntityRemovals(removalPlan))
            {
                return WorldGameplayTickRecordResult::ReplicationPlanFailed;
            }

            WorldResult<WorldGameplayReplicationPlan> broadcastPlanResult =
                gameplayReplicationPlanner_.BuildBroadcast(config_.gameplay, commitReport, committedJoinedSessions);
            if (broadcastPlanResult.Failed())
            {
                return WorldGameplayTickRecordResult::ReplicationPlanFailed;
            }
            WorldGameplayReplicationPlan broadcastPlan = broadcastPlanResult.TakeValue();
            if (commitReport.roundSnapshotChanged && commitReport.roundSnapshot.phase == WorldRoundPhase::Ended)
            {
                WorldResult<WorldGameplayReplicationPlan> roundResultPlanResult =
                    gameplayReplicationPlanner_.BuildRoundResults(serverTick, commitReport.roundSnapshot.roundId,
                                                                  gameplayState_.PlayerScores(),
                                                                  committedJoinedSessions);
                if (roundResultPlanResult.Failed())
                {
                    return WorldGameplayTickRecordResult::ReplicationPlanFailed;
                }
                WorldGameplayReplicationPlan roundResultPlan = roundResultPlanResult.TakeValue();
                broadcastPlan.roundResults = std::move(roundResultPlan.roundResults);
            }
            if (!broadcastPlan.scoreStates.empty() || broadcastPlan.hasRoundState ||
                !broadcastPlan.roundResults.empty())
            {
                pendingGameplayBroadcast_ = std::move(broadcastPlan);
                hasPendingGameplayBroadcast_ = true;
            }
            if (phaseActiveArea != nullptr)
            {
                latestOverviewActiveArea_ = *phaseActiveArea;
                hasLatestOverviewActiveArea_ = true;
            }
            return WorldGameplayTickRecordResult::Recorded;
        }
        catch (...)
        {
            return WorldGameplayTickRecordResult::OutboundRejected;
        }
    }

    bool WorldIngressEventConsumer::OutboundBatchFailed() const noexcept
    {
        return outboundBatchFailed_;
    }

    bool WorldIngressEventConsumer::FinalizePlayerBodies()
    {
        if (!WorldPlayerBody::IsEnabled(config_.join.playerBody))
        {
            return true;
        }

        std::vector<PreparedPlayerBodyUpdate> preparedUpdates;
        preparedUpdates.reserve(gameplayState_.PlayerCount());
        for (const WorldPlayerScore& score : gameplayState_.PlayerScores())
        {
            if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
            {
                continue;
            }
            if (score.lifecycle != WorldPlayerLifecycle::Alive)
            {
                return false;
            }

            EntityHandle entityHandle;
            WorldEntityComponents components;
            if (!score.controlledEntityKey.IsValid() ||
                !entityManager_.TryFindHandle(score.controlledEntityKey, &entityHandle) ||
                !entityManager_.TryReadComponents(entityHandle, &components) ||
                components.playerControl.playerId != score.playerId ||
                WorldPlayerBody::Finalize(config_.join.playerBody, score.score, &components) !=
                    WorldPlayerBodyUpdateResult::Updated)
            {
                return false;
            }
            preparedUpdates.push_back(PreparedPlayerBodyUpdate{entityHandle, std::move(components)});
        }

        for (PreparedPlayerBodyUpdate& update : preparedUpdates)
        {
            if (!entityManager_.TryReplaceComponents(update.entityHandle, update.components))
            {
                return false;
            }
        }
        return true;
    }

    // 매 tick 처리 시에 durable event 만 별도 기록
    bool WorldIngressEventConsumer::RecordDurableTickOutbound(
        const std::uint32_t serverTick, const std::uint32_t batchLastServerTick,
        const std::span<const WorldSession> joinedSessions) noexcept
    {
        // AOI Lifecycle 기록(Entity Spawn, Entity Remove, AOI Enter/Leave, Resource Spawn/Remove)
        const WorldAoiReplicationRecordResult aoiResult = RecordAoiReplication(
            serverTick, batchLastServerTick, joinedSessions, true, AoiReplicationContent::LifecycleOnly);

        // 새 player EntitySpawn이 먼저 기록된 뒤 해당 session의 controlled entity binding을 교체한다.
        if ((aoiResult != WorldAoiReplicationRecordResult::Skipped &&
             aoiResult != WorldAoiReplicationRecordResult::Recorded) ||
            !FlushPlayerSpawnPublications(serverTick) || !FlushGameplayBroadcast())
        {
            outboundBatchFailed_ = true;
            return false;
        }
        return true;
    }

    bool WorldIngressEventConsumer::FlushPlayerSpawnPublications(const std::uint32_t serverTick) noexcept
    {
        for (const WorldGameplayPlayerSpawn& spawn : pendingPlayerSpawns_)
        {
            const SessionKeyToSendChannelMap::const_iterator channelFound =
                sessionKeyToSendChannel_.find(spawn.sessionKey);
            EntityHandle spawnedEntityHandle;
            WorldEntityComponents spawnedComponents;
            if (channelFound == sessionKeyToSendChannel_.end() ||
                !entityManager_.TryFindHandle(spawn.spawnedEntityKey, &spawnedEntityHandle) ||
                !entityManager_.TryReadComponents(spawnedEntityHandle, &spawnedComponents))
            {
                return false;
            }

            WorldSession session;
            if (!sessionRegistry_.TryFind(spawn.sessionKey, &session) || session.playerId != spawn.playerId)
            {
                return false;
            }
            const protocol::v2::EntitySpawn entitySpawn{
                protocol::v1::EntitySpawn{
                    serverTick,
                    spawn.spawnedEntityKey.entityId,
                    spawn.spawnedEntityKey.generation,
                    static_cast<protocol::EntityKind>(spawnedComponents.replicationMetadata.entityKind),
                    spawnedComponents.replicationMetadata.archetypeId,
                    static_cast<protocol::ShapeKind>(spawnedComponents.replicationMetadata.primaryShapeKind),
                    spawnedComponents.replicationMetadata.primaryCircleRadius,
                    spawnedComponents.movementCapability.maxMoveSpeed,
                    spawnedComponents.transform.positionX,
                    spawnedComponents.transform.positionY,
                    spawnedComponents.motion.velocityX,
                    spawnedComponents.motion.velocityY,
                    spawnedComponents.transform.angleRadians,
                },
                spawn.playerId,
                session.displayName,
            };
            const protocol::v1::ControlledEntityRebind rebind{
                serverTick,
                spawn.playerId,
                spawn.previousEntityKey.entityId,
                spawn.previousEntityKey.generation,
                spawn.spawnedEntityKey.entityId,
                spawn.spawnedEntityKey.generation,
            };
            const std::size_t entitySpawnPayloadBytes =
                protocol::v2::EntitySpawn::CalculatePayloadBytes(entitySpawn.displayName);
            std::vector<std::byte> entitySpawnPayload(entitySpawnPayloadBytes);
            std::array<std::byte, protocol::v1::ControlledEntityRebind::Wire::PayloadBytes> rebindPayload;
            if (entitySpawnPayloadBytes == 0 ||
                protocol::v2::EntitySpawn::Encode(entitySpawn, entitySpawnPayload) !=
                    protocol::WorldProtocolError::Success ||
                protocol::v1::ControlledEntityRebind::Encode(rebind, rebindPayload) !=
                    protocol::WorldProtocolError::Success ||
                SubmitOutbound(channelFound->second,
                               psnr::core::NrPacketType{
                                   static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn),
                               },
                               psnr::runtime::NrByteView{
                                   entitySpawnPayload.data(),
                                   static_cast<std::uint32_t>(entitySpawnPayload.size()),
                               })
                    .Failed() ||
                SubmitOutbound(channelFound->second,
                               psnr::core::NrPacketType{
                                   static_cast<std::uint16_t>(protocol::S2CPacketType::ControlledEntityRebind),
                               },
                               psnr::runtime::NrByteView{
                                   rebindPayload.data(),
                                   static_cast<std::uint32_t>(rebindPayload.size()),
                               })
                    .Failed())
            {
                return false;
            }
        }

        pendingPlayerSpawns_.clear();
        return true;
    }

    WorldControlledStatePublishReport WorldIngressEventConsumer::PublishControlledEntityStates(
        const std::uint32_t firstProcessedServerTick, const std::uint32_t lastProcessedServerTick,
        const std::span<const WorldSession> joinedSessions) noexcept
    {
        WorldControlledStatePublishReport report;
        if (config_.join.snapshotIntervalTicks == 0 || firstProcessedServerTick > lastProcessedServerTick ||
            nextSnapshotTick_ > lastProcessedServerTick)
        {
            if (!FlushGameplayBroadcast())
            {
                outboundBatchFailed_ = true;
            }
            return report;
        }

        const std::uint64_t snapshotIntervalTicks = config_.join.snapshotIntervalTicks; // 전송 간격
        while (nextSnapshotTick_ < firstProcessedServerTick) // 이미 지나간 tick 정리 후 batch 범위로 이동
        {
            nextSnapshotTick_ += snapshotIntervalTicks;
        }
        if (nextSnapshotTick_ > lastProcessedServerTick) // 이번 batch 에 snapshot 시점이 없으면 종료
        {
            if (!FlushGameplayBroadcast())
            {
                outboundBatchFailed_ = true;
            }
            return report;
        }

        std::uint32_t scheduledSnapshotCount = 0;
        do
        {
            nextSnapshotTick_ += snapshotIntervalTicks;
            ++scheduledSnapshotCount;
        } while (nextSnapshotTick_ <= lastProcessedServerTick); // 예정 snapshot tick 을 전부 소비

        report.suppressedSnapshotCount = scheduledSnapshotCount - 1;
        report.snapshotPublished = true;
        for (const WorldSession& session : joinedSessions) // joined session 의 상태를 report
        {
            WorldPlayerScore score;
            if (gameplayEnabled_)
            {
                if (!gameplayState_.TryFindPlayerScore(session.playerId, &score))
                {
                    ++report.rejected;
                    continue;
                }
                if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
                {
                    continue;
                }
                if (score.lifecycle != WorldPlayerLifecycle::Alive)
                {
                    ++report.rejected;
                    continue;
                }
            }

            ++report.attempted;
            const SessionKeyToSendChannelMap::const_iterator channelFound =
                sessionKeyToSendChannel_.find(session.sessionKey);
            EntityHandle entityHandle;
            WorldEntityComponents components;
            if (channelFound == sessionKeyToSendChannel_.end() ||
                !entityManager_.TryFindHandle(session.entityKey, &entityHandle) ||
                !entityManager_.TryReadComponents(entityHandle, &components))
            {
                ++report.rejected;
                static_cast<void>(server_.RequestSessionClose(
                    session.sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                continue;
            }

            std::array<std::byte, protocol::v2::ControlledEntityState::Wire::MaximumPayloadBytes> payload{};
            std::size_t payloadBytes = protocol::v1::ControlledEntityState::Wire::PayloadBytes;
            protocol::WorldProtocolError encodeResult = protocol::WorldProtocolError::Success;
            if (gameplayEnabled_)
            {
                WorldResult<protocol::v2::ControlledEntityState> stateResult =
                    gameplayReplicationPlanner_.BuildControlledEntityState(lastProcessedServerTick, session, score,
                                                                           components);
                if (stateResult.Failed())
                {
                    ++report.rejected;
                    static_cast<void>(server_.RequestSessionClose(
                        session.sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                    continue;
                }
                protocol::v2::ControlledEntityState state = stateResult.TakeValue();
                payloadBytes =
                    protocol::v2::ControlledEntityState::Wire::CalculatePayloadBytes(state.bodyTrailSamples.size());
                encodeResult = protocol::v2::ControlledEntityState::Encode(
                    state, std::span<std::byte>{payload.data(), payloadBytes});
            }
            else
            {
                const protocol::v1::ControlledEntityState state{
                    lastProcessedServerTick,           session.entityKey.generation, components.transform.positionX,
                    components.transform.positionY,    components.motion.velocityX,  components.motion.velocityY,
                    components.transform.angleRadians,
                };
                encodeResult = protocol::v1::ControlledEntityState::Encode(
                    state, std::span<std::byte>{payload.data(), payloadBytes});
            }
            if (encodeResult != protocol::WorldProtocolError::Success)
            {
                ++report.rejected;
                static_cast<void>(server_.RequestSessionClose(
                    session.sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                continue;
            }

            const psnr::core::NrStatus submitStatus =
                SubmitOutbound(channelFound->second,
                               psnr::core::NrPacketType{
                                   static_cast<std::uint16_t>(protocol::S2CPacketType::ControlledEntityState),
                               },
                               psnr::runtime::NrByteView{
                                   payload.data(),
                                   static_cast<std::uint32_t>(payloadBytes),
                               });
            if (submitStatus.Failed())
            {
                ++report.rejected;
                static_cast<void>(server_.RequestSessionClose(
                    session.sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                continue;
            }

            ++report.submitted;
        }

        // publish 에는 fullsnapshot
        report.aoiReplication = RecordAoiReplication(lastProcessedServerTick, lastProcessedServerTick, joinedSessions,
                                                     true, AoiReplicationContent::FullSnapshot);
        if (report.aoiReplication != WorldAoiReplicationRecordResult::Skipped &&
            report.aoiReplication != WorldAoiReplicationRecordResult::Recorded)
        {
            outboundBatchFailed_ = true;
        }
        if (!FlushGameplayBroadcast())
        {
            outboundBatchFailed_ = true;
        }
        return report;
    }

    WorldOverviewPublishReport WorldIngressEventConsumer::PublishWorldOverview(
        const std::uint32_t firstProcessedServerTick, const std::uint32_t lastProcessedServerTick,
        const std::span<const WorldSession> joinedSessions) noexcept
    {
        WorldOverviewPublishReport report;
        if (!gameplayEnabled_)
        {
            return report;
        }

        WorldOverviewCadenceDecision decision;
        if (overviewCadence_.Evaluate(firstProcessedServerTick, lastProcessedServerTick, &decision) !=
            WorldOverviewCadenceResult::Evaluated)
        {
            outboundBatchFailed_ = true;
            return report;
        }
        report.suppressedOverviewCount = decision.suppressedOverviewCount;
        if (!decision.IsDue())
        {
            return report;
        }
        if (!hasLatestOverviewActiveArea_)
        {
            outboundBatchFailed_ = true;
            return report;
        }

        try
        {
            std::vector<psnr::runtime::NrSessionSendChannel> channels;
            channels.reserve(sessionRegistry_.Size());
            for (const WorldSession& session : joinedSessions)
            {
                const SessionKeyToSendChannelMap::const_iterator found =
                    sessionKeyToSendChannel_.find(session.sessionKey);
                if (!session.IsJoined() || found == sessionKeyToSendChannel_.end())
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
                channels.push_back(found->second);
            }
            for (const WorldSession& session : sessionRegistry_.RegisteredSessions())
            {
                if (!session.IsObserver())
                {
                    continue;
                }
                const SessionKeyToSendChannelMap::const_iterator found =
                    sessionKeyToSendChannel_.find(session.sessionKey);
                if (found == sessionKeyToSendChannel_.end())
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
                channels.push_back(found->second);
            }
            if (channels.empty())
            {
                return report;
            }

            const WorldPhysicsArenaBounds mapBounds{
                config_.join.arenaMinX,
                config_.join.arenaMinY,
                config_.join.arenaMaxX,
                config_.join.arenaMaxY,
            };
            WorldResult<WorldOverviewPlanInput> inputResult = gameplayReplicationPlanner_.BuildOverviewInput(
                lastProcessedServerTick, decision.overviewId, mapBounds, latestOverviewActiveArea_, gameplayState_,
                entityManager_);
            if (inputResult.Failed())
            {
                outboundBatchFailed_ = true;
                return report;
            }
            WorldOverviewPlanInput input = inputResult.TakeValue();
            for (WorldOverviewPlayerInput& player : input.alivePlayers)
            {
                const WorldSession* const session =
                    WorldSessionLookup::FindUniqueByPlayerId(joinedSessions, player.playerId);
                if (session == nullptr)
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
                player.displayName = session->displayName;
            }

            WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> chunksResult = overviewPlanner_.Plan(input);
            if (chunksResult.Failed())
            {
                outboundBatchFailed_ = true;
                return report;
            }
            const std::vector<protocol::v3::WorldOverviewSnapshot> chunks = chunksResult.TakeValue();

            std::vector<std::vector<std::byte>> encodedChunks;
            encodedChunks.reserve(chunks.size());
            std::size_t totalPayloadBytes = 0;
            for (const protocol::v3::WorldOverviewSnapshot& chunk : chunks)
            {
                const std::size_t payloadBytes = protocol::v3::WorldOverviewSnapshot::CalculatePayloadBytes(chunk);
                encodedChunks.emplace_back(payloadBytes);
                if (payloadBytes == 0 || protocol::v3::WorldOverviewSnapshot::Encode(chunk, encodedChunks.back()) !=
                                             protocol::WorldProtocolError::Success)
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
                totalPayloadBytes += payloadBytes;
            }

            if (outboundMode_ == WorldOutboundMode::DoubleBuffered)
            {
                if (outboundBuffer_ == nullptr ||
                    chunks.size() > std::numeric_limits<std::size_t>::max() / channels.size())
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
                const WorldOutboundBatchCapacity capacity = outboundBuffer_->CapacityPerSlot();
                const WorldOutboundBatchUsage usage = outboundBuffer_->WritableUsage();
                const std::size_t requiredRecipients = chunks.size() * channels.size();
                if (usage.recordCount > capacity.recordCount || usage.recipientCount > capacity.recipientCount ||
                    usage.payloadByteCount > capacity.payloadByteCount ||
                    chunks.size() > capacity.recordCount - usage.recordCount ||
                    requiredRecipients > capacity.recipientCount - usage.recipientCount ||
                    totalPayloadBytes > capacity.payloadByteCount - usage.payloadByteCount)
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
            }

            const psnr::core::NrPacketType packetType{
                static_cast<std::uint16_t>(protocol::S2CPacketType::WorldOverviewSnapshot),
            };
            for (const std::vector<std::byte>& payload : encodedChunks)
            {
                if (SubmitOutboundMany(channels, packetType, payload).Failed())
                {
                    outboundBatchFailed_ = true;
                    return report;
                }
            }

            report.recipientCount = static_cast<std::uint32_t>(channels.size());
            report.chunkCount = static_cast<std::uint32_t>(chunks.size());
            report.overviewPublished = true;
            return report;
        }
        catch (const std::bad_alloc&)
        {
            outboundBatchFailed_ = true;
            return report;
        }
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandleSessionAccepted(
        const WorldSessionKey sessionKey, const psnr::runtime::NrSessionSendChannel& sendChannel)
    {
        if (!sessionRegistry_.TryRegister(sessionKey))
        {
            return WorldIngressEventHandleResult::DuplicateSession;
        }

        const std::pair<SessionKeyToSendChannelMap::iterator, bool> inserted =
            sessionKeyToSendChannel_.emplace(sessionKey, sendChannel);
        if (!inserted.second)
        {
            static_cast<void>(sessionRegistry_.Remove(sessionKey));
            return WorldIngressEventHandleResult::DuplicateSession;
        }

        return WorldIngressEventHandleResult::SessionRegistered;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandleSessionClosed(
        const WorldSessionKey sessionKey, const psnr::runtime::NrSessionEndReason endReason)
    {
        WorldSession session;
        const SessionKeyToSendChannelMap::const_iterator channelFound = sessionKeyToSendChannel_.find(sessionKey);
        if (!sessionRegistry_.TryFind(sessionKey, &session) || channelFound == sessionKeyToSendChannel_.end())
        {
            return WorldIngressEventHandleResult::SessionNotFound;
        }

        if (endReason == psnr::runtime::NrSessionEndReason::ProtocolError)
        {
            ++metrics_.protocolErrorSessionClosedCount;
        }

        bool worldStateCleanupSucceeded = true;
        bool hasDisconnectDropSnapshot = false;
        WorldDisconnectDropSnapshot disconnectDropSnapshot;
        if (session.IsJoined())
        {
            bool entityRemovalRequired = true;
            if (gameplayEnabled_)
            {
                WorldPlayerScore score;
                if (!gameplayState_.TryFindPlayerScore(session.playerId, &score) ||
                    score.controlledEntityKey != session.entityKey ||
                    (score.lifecycle != WorldPlayerLifecycle::Alive &&
                     score.lifecycle != WorldPlayerLifecycle::SpawnPending))
                {
                    worldStateCleanupSucceeded = false;
                }
                else
                {
                    entityRemovalRequired = score.lifecycle == WorldPlayerLifecycle::Alive;
                    if (score.lifecycle == WorldPlayerLifecycle::Alive &&
                        gameplayState_.RoundState().phase == WorldRoundPhase::Running)
                    {
                        EntityHandle snapshotHandle;
                        WorldEntityComponents snapshotComponents;
                        if (!entityManager_.TryFindHandle(session.entityKey, &snapshotHandle) ||
                            !entityManager_.TryReadComponents(snapshotHandle, &snapshotComponents) ||
                            snapshotComponents.replicationMetadata.entityKind != WorldEntityKind::Player ||
                            snapshotComponents.playerControl.playerId != session.playerId)
                        {
                            worldStateCleanupSucceeded = false;
                        }
                        else
                        {
                            disconnectDropSnapshot = WorldDisconnectDropSnapshot{
                                session.playerId,
                                session.entityKey,
                                score.score,
                                snapshotComponents.transform,
                                std::move(snapshotComponents.bodyTrail),
                            };
                            hasDisconnectDropSnapshot = true;
                        }
                    }
                }
            }

            if (entityRemovalRequired)
            {
                EntityHandle entityHandle;
                if (!entityManager_.TryFindHandle(session.entityKey, &entityHandle) ||
                    !entityManager_.Remove(entityHandle))
                {
                    worldStateCleanupSucceeded = false;
                }
            }
            if (gameplayEnabled_ && !gameplayState_.RemovePlayer(session.playerId, session.entityKey))
            {
                worldStateCleanupSucceeded = false;
            }
        }

        if (worldStateCleanupSucceeded && hasDisconnectDropSnapshot)
        {
            try
            {
                pendingDisconnectDropSnapshots_.push_back(std::move(disconnectDropSnapshot));
            }
            catch (const std::bad_alloc&)
            {
                worldStateCleanupSucceeded = false;
            }
        }

        sessionKeyToSendChannel_.erase(channelFound);
        sessionKeyToProtocolViolationWindow_.erase(sessionKey);
        static_cast<void>(aoiPlanner_.RemoveSession(sessionKey));
        initialAoiRecipientsThisTick_.erase(
            std::remove(initialAoiRecipientsThisTick_.begin(), initialAoiRecipientsThisTick_.end(), sessionKey),
            initialAoiRecipientsThisTick_.end());
        static_cast<void>(movementCommandStore_.RemoveSession(sessionKey));
        static_cast<void>(sessionRegistry_.Remove(sessionKey));
        if (worldStateCleanupSucceeded && !ResetEndedRoundIfEmpty())
        {
            worldStateCleanupSucceeded = false;
        }
        applicationEventSink_.RecordSessionCleanup(WorldSessionCleanupApplicationEvent{
            worldStateCleanupSucceeded ? WorldSessionCleanupApplicationEventKind::Completed
                                       : WorldSessionCleanupApplicationEventKind::Failed,
            currentServerTick_,
            sessionKey,
            session.entityKey,
        });
        return worldStateCleanupSucceeded ? WorldIngressEventHandleResult::SessionRemoved
                                          : WorldIngressEventHandleResult::WorldStateCleanupFailed;
    }

    bool WorldIngressEventConsumer::ResetEndedRoundIfEmpty() noexcept
    {
        const WorldRoundRuntimeState& roundState = gameplayState_.RoundState();
        if (!gameplayEnabled_ || roundState.phase != WorldRoundPhase::Ended || gameplayState_.PlayerCount() != 0)
        {
            return true;
        }

        const WorldGameplayPhaseResult resetResult{
            currentServerTick_,
            {},
            {},
            {},
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::ResetToWaiting,
                WorldRoundRuntimeState{roundState.roundId + 1, WorldRoundPhase::Waiting, 0, 0},
            },
        };
        WorldGameplayCommitReport commitReport;
        if (WorldGameplayCommitter::Commit(config_.gameplay, resetResult, entityManager_, gameplayState_,
                                           &commitReport) != WorldGameplayCommitResult::Committed)
        {
            return false;
        }

        latestOverviewActiveArea_ = WorldActiveArea{};
        hasLatestOverviewActiveArea_ = false;
        return true;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandlePacket(const WorldSessionKey sessionKey,
                                                                          const psnr::core::NrPacketType packetType,
                                                                          const psnr::runtime::NrByteView payload,
                                                                          const WorldInboundMode inboundMode) noexcept
    {
        if (!sessionKeyToSendChannel_.contains(sessionKey))
        {
            return WorldIngressEventHandleResult::SessionNotFound;
        }

        if (packetType.value == static_cast<std::uint16_t>(protocol::C2SPacketType::WorldTimeSyncRequest))
        {
            return HandleTimeSync(sessionKey, payload);
        }
        if (packetType.value == static_cast<std::uint16_t>(protocol::C2SPacketType::JoinWorldRequest))
        {
            return HandleJoin(sessionKey, payload);
        }
        if (packetType.value == static_cast<std::uint16_t>(protocol::C2SPacketType::ObserveWorldRequest))
        {
            return HandleObserve(sessionKey, payload);
        }
        if (gameplayEnabled_ && gameplayState_.RoundState().phase == WorldRoundPhase::Ended)
        {
            return WorldIngressEventHandleResult::PacketRejected;
        }

        const WorldIngressAdmissionContext context{sessionRegistry_, sessionKey};
        WorldControlCommand controlCommand;
        const WorldIngressPacketRouteResult routeResult = WorldIngressPacketRouter::Route(
            context, inboundMode, currentServerTick_, packetType.value,
            std::span<const std::byte>(payload.data, payload.size), movementCommandStore_, &controlCommand);

        return HandleRouteResult(sessionKey, routeResult, controlCommand);
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandleJoin(const WorldSessionKey sessionKey,
                                                                        const psnr::runtime::NrByteView payload)
    {
        if (gameplayEnabled_ && gameplayState_.RoundState().phase == WorldRoundPhase::Ended)
        {
            return WorldIngressEventHandleResult::JoinRejected;
        }
        constexpr std::uint64_t MaximumPlayerId = std::numeric_limits<std::uint32_t>::max();
        if (nextPlayerId_ == 0 || nextPlayerId_ > MaximumPlayerId)
        {
            return WorldIngressEventHandleResult::JoinRejected;
        }

        const SessionKeyToSendChannelMap::const_iterator channelFound = sessionKeyToSendChannel_.find(sessionKey);
        if (channelFound == sessionKeyToSendChannel_.end())
        {
            return WorldIngressEventHandleResult::SessionNotFound;
        }

        // Prepare
        const std::uint32_t playerId = static_cast<std::uint32_t>(nextPlayerId_);
        WorldResult<WorldJoinBaseline> joinResult =
            WorldJoinIngress::Prepare(sessionRegistry_, entityManager_, sessionKey, playerId, currentServerTick_,
                                      config_.join, std::span<const std::byte>(payload.data, payload.size));
        if (joinResult.Failed())
        {
            if (joinResult.Error() == WorldErrorCode::MalformedPayload)
            {
                ++metrics_.malformedPayloadCount;
                return RequestProtocolClose(sessionKey, WorldProtocolCloseCause::MalformedPayload);
            }
            return WorldIngressEventHandleResult::JoinRejected;
        }
        const WorldJoinBaseline baseline = joinResult.TakeValue();

        const std::size_t entitySpawnPayloadBytes =
            protocol::v2::EntitySpawn::CalculatePayloadBytes(baseline.entitySpawn.displayName);
        std::vector<std::byte> entitySpawnPayload(entitySpawnPayloadBytes);
        std::array<std::byte, protocol::v2::WorldReady::Wire::MaximumPayloadBytes> worldReadyPayloadStorage{};
        const std::size_t worldReadyPayloadBytes =
            protocol::v2::WorldReady::CalculatePayloadBytes(baseline.worldReady.displayName);
        const std::span<std::byte> worldReadyPayload{worldReadyPayloadStorage.data(), worldReadyPayloadBytes};
        if (entitySpawnPayloadBytes == 0 ||
            protocol::v2::EntitySpawn::Encode(baseline.entitySpawn, entitySpawnPayload) !=
                protocol::WorldProtocolError::Success ||
            protocol::v2::WorldReady::Encode(baseline.worldReady, worldReadyPayload) !=
                protocol::WorldProtocolError::Success)
        {
            const bool rollbackSucceeded =
                RollbackPreparedJoin(sessionKey, playerId, baseline, false, WorldJoinFailureStage::BaselineEncoding);
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return rollbackSucceeded ? WorldIngressEventHandleResult::JoinRejected
                                     : WorldIngressEventHandleResult::WorldStateCleanupFailed;
        }

        bool gameplayPlayerRegistered = false;
        if (gameplayEnabled_)
        {
            if (gameplayState_.TryRegisterPlayer(playerId, baseline.entityKey) !=
                WorldPlayerScoreRegisterResult::Registered)
            {
                const bool rollbackSucceeded = RollbackPreparedJoin(sessionKey, playerId, baseline, false,
                                                                    WorldJoinFailureStage::GameplayRegistration);
                static_cast<void>(server_.RequestSessionClose(
                    sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                return rollbackSucceeded ? WorldIngressEventHandleResult::JoinRejected
                                         : WorldIngressEventHandleResult::WorldStateCleanupFailed;
            }
            gameplayPlayerRegistered = true;
        }

        const psnr::core::NrStatus entitySpawnSubmitStatus =
            SubmitOutbound(channelFound->second,
                           psnr::core::NrPacketType{
                               static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn),
                           },
                           psnr::runtime::NrByteView{
                               entitySpawnPayload.data(),
                               static_cast<std::uint32_t>(entitySpawnPayload.size()),
                           });
        if (entitySpawnSubmitStatus.Failed())
        {
            const bool rollbackSucceeded = RollbackPreparedJoin(
                sessionKey, playerId, baseline, gameplayPlayerRegistered, WorldJoinFailureStage::EntitySpawnSubmission);
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return rollbackSucceeded ? WorldIngressEventHandleResult::RuntimeSubmitFailed
                                     : WorldIngressEventHandleResult::WorldStateCleanupFailed;
        }

        const bool aoiReplicationEnabled = AoiReplicationEnabled();
        const WorldSession preparedSession{sessionKey, playerId, baseline.entityKey, baseline.worldReady.displayName};
        if (aoiReplicationEnabled)
        {
            const WorldAoiReplicationRecordResult aoiResult = RecordAoiReplication(
                currentServerTick_, outboundBatchLastServerTick_, std::span<const WorldSession>{&preparedSession, 1},
                false, AoiReplicationContent::LifecycleOnly);
            if (aoiResult != WorldAoiReplicationRecordResult::Recorded)
            {
                outboundBatchFailed_ = true;
                const bool rollbackSucceeded =
                    RollbackPreparedJoin(sessionKey, playerId, baseline, gameplayPlayerRegistered,
                                         WorldJoinFailureStage::AoiBaselineRecording);
                static_cast<void>(server_.RequestSessionClose(
                    sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                return rollbackSucceeded ? WorldIngressEventHandleResult::RuntimeSubmitFailed
                                         : WorldIngressEventHandleResult::WorldStateCleanupFailed;
            }
        }

        if (!RecordJoinGameplayBaseline(channelFound->second))
        {
            outboundBatchFailed_ = true;
            const bool rollbackSucceeded =
                RollbackPreparedJoin(sessionKey, playerId, baseline, gameplayPlayerRegistered,
                                     WorldJoinFailureStage::GameplayBaselineRecording);
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return rollbackSucceeded ? WorldIngressEventHandleResult::RuntimeSubmitFailed
                                     : WorldIngressEventHandleResult::WorldStateCleanupFailed;
        }

        const psnr::core::NrStatus worldReadySubmitStatus =
            SubmitOutbound(channelFound->second,
                           psnr::core::NrPacketType{
                               static_cast<std::uint16_t>(protocol::S2CPacketType::WorldReady),
                           },
                           psnr::runtime::NrByteView{
                               worldReadyPayload.data(),
                               static_cast<std::uint32_t>(worldReadyPayload.size()),
                           });
        if (worldReadySubmitStatus.Failed())
        {
            const bool rollbackSucceeded = RollbackPreparedJoin(
                sessionKey, playerId, baseline, gameplayPlayerRegistered, WorldJoinFailureStage::WorldReadySubmission);
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return rollbackSucceeded ? WorldIngressEventHandleResult::RuntimeSubmitFailed
                                     : WorldIngressEventHandleResult::WorldStateCleanupFailed;
        }

        if (aoiReplicationEnabled)
        {
            const WorldAoiReplicationRecordResult aoiResult = RecordAoiReplication(
                currentServerTick_, outboundBatchLastServerTick_, std::span<const WorldSession>{&preparedSession, 1},
                false, AoiReplicationContent::FullSnapshot);
            if (aoiResult != WorldAoiReplicationRecordResult::Recorded)
            {
                outboundBatchFailed_ = true;
                const bool rollbackSucceeded =
                    RollbackPreparedJoin(sessionKey, playerId, baseline, gameplayPlayerRegistered,
                                         WorldJoinFailureStage::AoiBaselineRecording);
                static_cast<void>(server_.RequestSessionClose(
                    sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
                return rollbackSucceeded ? WorldIngressEventHandleResult::RuntimeSubmitFailed
                                         : WorldIngressEventHandleResult::WorldStateCleanupFailed;
            }
        }

        // 신규 Client에게 EntitySpawn, WorldReady, 초기 AOI baseline 전송을 위한 Outbound 등록 이후에 Commit
        if (!WorldJoinIngress::Commit(sessionRegistry_, entityManager_, sessionKey, playerId, baseline))
        {
            const bool rollbackSucceeded = RollbackPreparedJoin(
                sessionKey, playerId, baseline, gameplayPlayerRegistered, WorldJoinFailureStage::Commit);
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return rollbackSucceeded ? WorldIngressEventHandleResult::JoinRejected
                                     : WorldIngressEventHandleResult::WorldStateCleanupFailed;
        }

        ++nextPlayerId_;
        if (aoiReplicationEnabled)
        {
            // 이번 tick에 join 을 성공하고, 초기 AOI Baseline 등록한 목록에 추가
            initialAoiRecipientsThisTick_.push_back(sessionKey);
        }
        applicationEventSink_.RecordJoin(WorldJoinApplicationEvent{
            WorldJoinApplicationEventKind::Committed,
            WorldJoinFailureStage::None,
            currentServerTick_,
            sessionKey,
            baseline.entityKey,
        });
        return WorldIngressEventHandleResult::JoinBaselineSubmitted;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandleObserve(
        const WorldSessionKey sessionKey, const psnr::runtime::NrByteView payload) noexcept
    {
        protocol::v1::ObserveWorldRequest request;
        if (protocol::v1::ObserveWorldRequest::Decode(std::span<const std::byte>(payload.data, payload.size),
                                                      &request) != protocol::WorldProtocolError::Success)
        {
            ++metrics_.malformedPayloadCount;
            return RequestProtocolClose(sessionKey, WorldProtocolCloseCause::MalformedPayload);
        }

        WorldSession session;
        const SessionKeyToSendChannelMap::const_iterator channelFound = sessionKeyToSendChannel_.find(sessionKey);
        if (!sessionRegistry_.TryFind(sessionKey, &session) || channelFound == sessionKeyToSendChannel_.end())
        {
            return WorldIngressEventHandleResult::SessionNotFound;
        }
        if (session.role != WorldSessionRole::Connected)
        {
            return WorldIngressEventHandleResult::ObserverRejected;
        }

        const protocol::v1::ObserverReady ready{
            currentServerTick_,     config_.join.tickRateHz, config_.join.arenaMinX, config_.join.arenaMinY,
            config_.join.arenaMaxX, config_.join.arenaMaxY,  config_.join.channelId,
        };
        std::array<std::byte, protocol::v1::ObserverReady::Wire::PayloadBytes> readyPayload;
        if (protocol::v1::ObserverReady::Encode(ready, readyPayload) != protocol::WorldProtocolError::Success ||
            !RecordObserverGameplayBaseline(channelFound->second) ||
            SubmitOutbound(channelFound->second,
                           psnr::core::NrPacketType{
                               static_cast<std::uint16_t>(protocol::S2CPacketType::ObserverReady),
                           },
                           psnr::runtime::NrByteView{
                               readyPayload.data(),
                               static_cast<std::uint32_t>(readyPayload.size()),
                           })
                .Failed())
        {
            outboundBatchFailed_ = true;
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return WorldIngressEventHandleResult::RuntimeSubmitFailed;
        }

        if (!sessionRegistry_.TryBindObserver(sessionKey))
        {
            static_cast<void>(server_.RequestSessionClose(
                sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ApplicationPolicy));
            return WorldIngressEventHandleResult::ObserverRejected;
        }
        return WorldIngressEventHandleResult::ObserverBaselineSubmitted;
    }

    bool WorldIngressEventConsumer::RollbackPreparedJoin(const WorldSessionKey sessionKey, const std::uint32_t playerId,
                                                         const WorldJoinBaseline& baseline,
                                                         const bool gameplayPlayerRegistered,
                                                         const WorldJoinFailureStage failureStage) noexcept
    {
        static_cast<void>(aoiPlanner_.RemoveSession(sessionKey));

        bool rollbackSucceeded = true;
        if (gameplayPlayerRegistered && !gameplayState_.RemovePlayer(playerId, baseline.entityKey))
        {
            rollbackSucceeded = false;
        }
        if (!WorldJoinIngress::Rollback(entityManager_, baseline))
        {
            rollbackSucceeded = false;
        }
        applicationEventSink_.RecordJoin(WorldJoinApplicationEvent{
            rollbackSucceeded ? WorldJoinApplicationEventKind::RolledBack
                              : WorldJoinApplicationEventKind::RollbackFailed,
            failureStage,
            currentServerTick_,
            sessionKey,
            baseline.entityKey,
        });
        return rollbackSucceeded;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandleTimeSync(
        const WorldSessionKey sessionKey, const psnr::runtime::NrByteView payload) noexcept
    {
        const WorldIngressAdmissionContext context{sessionRegistry_, sessionKey};
        protocol::v1::WorldTimeSyncResponse response;
        const WorldIngressAdmissionResult admissionResult = WorldTimeSyncIngress::Admit(
            context, lastCompletedServerTick_, std::span<const std::byte>(payload.data, payload.size), &response);
        if (admissionResult != WorldIngressAdmissionResult::Accepted)
        {
            if (admissionResult == WorldIngressAdmissionResult::MalformedPayload)
            {
                ++metrics_.malformedPayloadCount;
                return RequestProtocolClose(sessionKey, WorldProtocolCloseCause::MalformedPayload);
            }
            return WorldIngressEventHandleResult::TimeSyncRejected;
        }

        std::array<std::byte, protocol::v1::WorldTimeSyncResponse::Wire::PayloadBytes> responsePayload;
        if (protocol::v1::WorldTimeSyncResponse::Encode(response, responsePayload) !=
            protocol::WorldProtocolError::Success)
        {
            return WorldIngressEventHandleResult::TimeSyncRejected;
        }

        const SessionKeyToSendChannelMap::const_iterator channelFound = sessionKeyToSendChannel_.find(sessionKey);
        if (channelFound == sessionKeyToSendChannel_.end())
        {
            return WorldIngressEventHandleResult::SessionNotFound;
        }

        const psnr::core::NrStatus submitStatus =
            SubmitOutbound(channelFound->second,
                           psnr::core::NrPacketType{
                               static_cast<std::uint16_t>(protocol::S2CPacketType::WorldTimeSyncResponse),
                           },
                           psnr::runtime::NrByteView{
                               responsePayload.data(),
                               static_cast<std::uint32_t>(responsePayload.size()),
                           });
        return submitStatus.Succeeded() ? WorldIngressEventHandleResult::TimeSyncSubmitted
                                        : WorldIngressEventHandleResult::RuntimeSubmitFailed;
    }

    std::size_t WorldIngressEventConsumer::SessionChannelCount() const noexcept
    {
        return sessionKeyToSendChannel_.size();
    }

    bool WorldIngressEventConsumer::AoiReplicationEnabled() const noexcept
    {
        return spatialIndex_ != nullptr && outboundMode_ == WorldOutboundMode::DoubleBuffered &&
               outboundBuffer_ != nullptr;
    }

    WorldIngressMetrics WorldIngressEventConsumer::Metrics() const noexcept
    {
        return metrics_;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::ApplyControlCommand(
        const WorldControlCommand& command) noexcept
    {
        EntityHandle entityHandle;
        WorldEntityComponents components;
        if (!entityManager_.TryFindHandle(command.entityKey, &entityHandle) ||
            !entityManager_.TryReadComponents(entityHandle, &components) ||
            components.playerControl.playerId != command.playerId)
        {
            return WorldIngressEventHandleResult::PacketRejected;
        }

        if (command.inputSequence <= components.playerControl.lastInputSequence)
        {
            return WorldIngressEventHandleResult::PacketDropped;
        }

        components.playerControl.lastInputSequence = command.inputSequence;
        components.playerControl.turnState = command.turnState;
        components.playerControl.boostState = command.boostState;
        return entityManager_.TryReplaceComponents(entityHandle, components)
                   ? WorldIngressEventHandleResult::ControlApplied
                   : WorldIngressEventHandleResult::PacketRejected;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::HandleRouteResult(
        const WorldSessionKey sessionKey, const WorldIngressPacketRouteResult routeResult,
        const WorldControlCommand& controlCommand) noexcept
    {
        switch (routeResult)
        {
        case WorldIngressPacketRouteResult::ControlAdmitted:
            return ApplyControlCommand(controlCommand);
        case WorldIngressPacketRouteResult::MovementStored:
            return WorldIngressEventHandleResult::MovementStored;
        case WorldIngressPacketRouteResult::MalformedPayload:
            ++metrics_.malformedPayloadCount;
            return RequestProtocolClose(sessionKey, WorldProtocolCloseCause::MalformedPayload);
        case WorldIngressPacketRouteResult::StaleEntityGeneration:
            ++metrics_.staleEntityGenerationDropCount;
            return WorldIngressEventHandleResult::PacketDropped;
        case WorldIngressPacketRouteResult::LateTargetTick:
            ++metrics_.lateTargetTickDropCount;
            return WorldIngressEventHandleResult::PacketDropped;
        case WorldIngressPacketRouteResult::TargetTickTooFarAhead:
            ++metrics_.futureTargetTickViolationCount;
            return RecordRateLimitedViolation(sessionKey) ? WorldIngressEventHandleResult::ProtocolCloseRequested
                                                          : WorldIngressEventHandleResult::PacketRejected;
        case WorldIngressPacketRouteResult::DuplicateMovementInput:
            ++metrics_.duplicateMovementInputViolationCount;
            return RecordRateLimitedViolation(sessionKey) ? WorldIngressEventHandleResult::ProtocolCloseRequested
                                                          : WorldIngressEventHandleResult::PacketRejected;
        case WorldIngressPacketRouteResult::InvalidArgument:
        case WorldIngressPacketRouteResult::SessionNotFound:
        case WorldIngressPacketRouteResult::SessionNotJoined:
        case WorldIngressPacketRouteResult::UnsupportedPacketType:
        case WorldIngressPacketRouteResult::UnknownPacketType:
            return WorldIngressEventHandleResult::PacketRejected;
        }

        return WorldIngressEventHandleResult::PacketRejected;
    }

    WorldIngressEventHandleResult WorldIngressEventConsumer::RequestProtocolClose(
        const WorldSessionKey sessionKey, const WorldProtocolCloseCause cause) noexcept
    {
        ++metrics_.protocolCloseRequestCount;
        const psnr::core::NrStatus closeStatus =
            server_.RequestSessionClose(sessionKey.value, psnr::runtime::NrSessionCloseRequestReason::ProtocolError);
        if (closeStatus.Succeeded())
        {
            ++metrics_.protocolCloseRequestSuccessCount;
        }
        else
        {
            ++metrics_.protocolCloseRequestFailureCount;
        }
        applicationEventSink_.RecordProtocolClose(WorldProtocolCloseApplicationEvent{
            closeStatus.Succeeded() ? WorldProtocolCloseApplicationEventKind::Requested
                                    : WorldProtocolCloseApplicationEventKind::RequestFailed,
            cause,
            currentServerTick_,
            sessionKey,
        });
        return WorldIngressEventHandleResult::ProtocolCloseRequested;
    }

    bool WorldIngressEventConsumer::RecordRateLimitedViolation(const WorldSessionKey sessionKey) noexcept
    {
        ProtocolViolationWindow& window = sessionKeyToProtocolViolationWindow_[sessionKey];
        // 10초가 몇 tick 인지
        const std::uint64_t violationWindowTicks = static_cast<std::uint64_t>(config_.join.tickRateHz) * 10;
        const bool windowExpired =
            currentServerTick_ < window.firstViolationServerTick // 현재 tick 이 첫 violation 발생 tick 보다 작음
            || static_cast<std::uint64_t>(currentServerTick_ - window.firstViolationServerTick) >= violationWindowTicks;
        // 첫 위반 이후 몇 tick 이 지났는지 계산해서 10초 기준 tick 보다 큰지 -> 크면 초기화 대상

        if (window.violationCount == 0 || windowExpired)
        {
            // window 초기화
            window.firstViolationServerTick = currentServerTick_;
            window.violationCount = 0;
            window.closeRequested = false;
        }

        ++window.violationCount;
        if (window.violationCount < 3 || window.closeRequested)
        {
            return window.closeRequested;
        }

        window.closeRequested = true;
        static_cast<void>(RequestProtocolClose(sessionKey, WorldProtocolCloseCause::RateLimitedViolation));
        return true;
    }

    psnr::core::NrStatus WorldIngressEventConsumer::SubmitOutbound(const psnr::runtime::NrSessionSendChannel& channel,
                                                                   const psnr::core::NrPacketType packetType,
                                                                   const psnr::runtime::NrByteView payload) noexcept
    {
        if (outboundMode_ == WorldOutboundMode::Direct)
        {
            return gateway_.Submit(channel, packetType, payload);
        }
        if (outboundMode_ != WorldOutboundMode::DoubleBuffered || outboundBuffer_ == nullptr)
        {
            outboundBatchFailed_ = true;
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
        }

        const std::array<psnr::runtime::NrSessionSendChannel, 1> recipients{channel};
        const WorldOutboundAppendResult appendResult =
            outboundBuffer_->TryAppend(packetType, recipients, std::span<const std::byte>{payload.data, payload.size});
        if (appendResult != WorldOutboundAppendResult::Appended)
        {
            outboundBatchFailed_ = true;
            const psnr::core::NrErrorCode errorCode = appendResult == WorldOutboundAppendResult::CapacityExceeded
                                                          ? psnr::core::NrErrorCode::CapacityExceeded
                                                          : psnr::core::NrErrorCode::InvalidState;
            return psnr::core::NrStatus::Failure(errorCode);
        }
        return psnr::core::NrStatus::Success();
    }

    psnr::core::NrStatus WorldIngressEventConsumer::SubmitOutboundMany(
        const std::span<const psnr::runtime::NrSessionSendChannel> channels, const psnr::core::NrPacketType packetType,
        const std::span<const std::byte> payload) noexcept
    {
        if (channels.empty() || channels.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }
        if (outboundMode_ == WorldOutboundMode::Direct)
        {
            psnr::runtime::NrGatewaySendReport sendReport;
            return gateway_.SubmitMany(
                psnr::runtime::NrSessionSendChannelView{channels.data(), static_cast<std::uint32_t>(channels.size())},
                packetType, psnr::runtime::NrByteView{payload.data(), static_cast<std::uint32_t>(payload.size())},
                sendReport);
        }
        if (outboundMode_ != WorldOutboundMode::DoubleBuffered || outboundBuffer_ == nullptr)
        {
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
        }

        const WorldOutboundAppendResult appendResult = outboundBuffer_->TryAppend(packetType, channels, payload);
        if (appendResult == WorldOutboundAppendResult::Appended)
        {
            return psnr::core::NrStatus::Success();
        }
        const psnr::core::NrErrorCode errorCode = appendResult == WorldOutboundAppendResult::CapacityExceeded
                                                      ? psnr::core::NrErrorCode::CapacityExceeded
                                                      : psnr::core::NrErrorCode::InvalidState;
        return psnr::core::NrStatus::Failure(errorCode);
    }

    bool WorldIngressEventConsumer::RecordGameplayEntityRemovals(const WorldGameplayReplicationPlan& plan) noexcept
    {
        for (const WorldGameplayEntityRemovalPlan& removal : plan.entityRemovals)
        {
            const SessionKeyToSendChannelMap::const_iterator channelFound =
                sessionKeyToSendChannel_.find(removal.sessionKey);
            std::array<std::byte, protocol::v1::EntityRemove::Wire::PayloadBytes> payload;
            if (channelFound == sessionKeyToSendChannel_.end() ||
                protocol::v1::EntityRemove::Encode(removal.entityRemove, payload) !=
                    protocol::WorldProtocolError::Success ||
                SubmitOutbound(channelFound->second,
                               psnr::core::NrPacketType{
                                   static_cast<std::uint16_t>(protocol::S2CPacketType::EntityRemove),
                               },
                               psnr::runtime::NrByteView{
                                   payload.data(),
                                   static_cast<std::uint32_t>(payload.size()),
                               })
                    .Failed())
            {
                outboundBatchFailed_ = true;
                return false;
            }
        }
        return true;
    }

    bool WorldIngressEventConsumer::RecordRoundResults(const WorldGameplayReplicationPlan& plan) noexcept
    {
        if (plan.roundResults.empty())
        {
            return true;
        }

        try
        {
            std::vector<WorldGameplayRoundResultPlan> publications = plan.roundResults;
            std::vector<psnr::runtime::NrSessionSendChannel> channels;
            channels.reserve(plan.roundResults.size() + sessionRegistry_.Size());
            for (const WorldGameplayRoundResultPlan& roundResult : plan.roundResults)
            {
                const SessionKeyToSendChannelMap::const_iterator found =
                    sessionKeyToSendChannel_.find(roundResult.sessionKey);
                if (found == sessionKeyToSendChannel_.end())
                {
                    return false;
                }
                channels.push_back(found->second);
            }

            protocol::v2::RoundResult observerResult = plan.roundResults.front().roundResult;
            observerResult.recipientFinalGrowthPoint = 0;
            for (const WorldSession& session : sessionRegistry_.RegisteredSessions())
            {
                if (!session.IsObserver())
                {
                    continue;
                }
                const SessionKeyToSendChannelMap::const_iterator found =
                    sessionKeyToSendChannel_.find(session.sessionKey);
                if (found == sessionKeyToSendChannel_.end())
                {
                    return false;
                }
                publications.push_back(WorldGameplayRoundResultPlan{session.sessionKey, observerResult});
                channels.push_back(found->second);
            }

            if (outboundMode_ == WorldOutboundMode::DoubleBuffered && outboundBuffer_ != nullptr)
            {
                return replicationPublisher_.RecordRoundResults(publications, channels, *outboundBuffer_) ==
                       WorldReplicationRecordResult::Recorded;
            }

            for (std::size_t index = 0; index < publications.size(); ++index)
            {
                const protocol::v2::RoundResult& roundResult = publications[index].roundResult;
                const std::size_t payloadBytes =
                    protocol::v2::RoundResult::Wire::CalculatePayloadBytes(roundResult.winnerPlayerIds.size());
                std::vector<std::byte> payload(payloadBytes);
                if (payloadBytes == 0 ||
                    protocol::v2::RoundResult::Encode(roundResult, payload) != protocol::WorldProtocolError::Success ||
                    SubmitOutbound(channels[index],
                                   psnr::core::NrPacketType{
                                       static_cast<std::uint16_t>(protocol::S2CPacketType::RoundResult),
                                   },
                                   psnr::runtime::NrByteView{
                                       payload.data(),
                                       static_cast<std::uint32_t>(payload.size()),
                                   })
                        .Failed())
                {
                    return false;
                }
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
    }

    // Score, Round 등 중요도 높은 데이터 전송
    bool WorldIngressEventConsumer::FlushGameplayBroadcast() noexcept
    {
        if (!hasPendingGameplayBroadcast_)
        {
            return true;
        }

        for (const protocol::v1::ScoreState& scoreState : pendingGameplayBroadcast_.scoreStates)
        {
            std::array<std::byte, protocol::v1::ScoreState::Wire::PayloadBytes> payload;
            if (protocol::v1::ScoreState::Encode(scoreState, payload) != protocol::WorldProtocolError::Success)
            {
                outboundBatchFailed_ = true;
                return false;
            }
            for (const WorldSessionKey recipient : pendingGameplayBroadcast_.worldBroadcastRecipients)
            {
                const SessionKeyToSendChannelMap::const_iterator channelFound =
                    sessionKeyToSendChannel_.find(recipient);
                if (channelFound == sessionKeyToSendChannel_.end() ||
                    SubmitOutbound(channelFound->second,
                                   psnr::core::NrPacketType{
                                       static_cast<std::uint16_t>(protocol::S2CPacketType::ScoreState),
                                   },
                                   psnr::runtime::NrByteView{
                                       payload.data(),
                                       static_cast<std::uint32_t>(payload.size()),
                                   })
                        .Failed())
                {
                    outboundBatchFailed_ = true;
                    return false;
                }
            }
        }

        if (pendingGameplayBroadcast_.hasRoundState)
        {
            std::array<std::byte, protocol::v1::RoundState::Wire::PayloadBytes> payload;
            if (protocol::v1::RoundState::Encode(pendingGameplayBroadcast_.roundState, payload) !=
                protocol::WorldProtocolError::Success)
            {
                outboundBatchFailed_ = true;
                return false;
            }
            for (const WorldSessionKey recipient : pendingGameplayBroadcast_.worldBroadcastRecipients)
            {
                const SessionKeyToSendChannelMap::const_iterator channelFound =
                    sessionKeyToSendChannel_.find(recipient);
                if (channelFound == sessionKeyToSendChannel_.end() ||
                    SubmitOutbound(channelFound->second,
                                   psnr::core::NrPacketType{
                                       static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState),
                                   },
                                   psnr::runtime::NrByteView{
                                       payload.data(),
                                       static_cast<std::uint32_t>(payload.size()),
                                   })
                        .Failed())
                {
                    outboundBatchFailed_ = true;
                    return false;
                }
            }
            for (const WorldSession& session : sessionRegistry_.RegisteredSessions())
            {
                if (!session.IsObserver())
                {
                    continue;
                }
                const SessionKeyToSendChannelMap::const_iterator channelFound =
                    sessionKeyToSendChannel_.find(session.sessionKey);
                if (channelFound == sessionKeyToSendChannel_.end() ||
                    SubmitOutbound(channelFound->second,
                                   psnr::core::NrPacketType{
                                       static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState),
                                   },
                                   psnr::runtime::NrByteView{
                                       payload.data(),
                                       static_cast<std::uint32_t>(payload.size()),
                                   })
                        .Failed())
                {
                    outboundBatchFailed_ = true;
                    return false;
                }
            }
        }

        if (!RecordRoundResults(pendingGameplayBroadcast_))
        {
            outboundBatchFailed_ = true;
            return false;
        }

        pendingGameplayBroadcast_ = WorldGameplayReplicationPlan{};
        hasPendingGameplayBroadcast_ = false;
        return true;
    }

    bool WorldIngressEventConsumer::RecordJoinGameplayBaseline(
        const psnr::runtime::NrSessionSendChannel& channel) noexcept
    {
        if (!gameplayEnabled_)
        {
            return true;
        }

        WorldResult<WorldGameplayReplicationPlan> planResult =
            gameplayReplicationPlanner_.BuildJoinBaseline(currentServerTick_, config_.gameplay, gameplayState_);
        if (planResult.Failed())
        {
            return false;
        }
        WorldGameplayReplicationPlan plan = planResult.TakeValue();

        for (const protocol::v1::ScoreState& scoreState : plan.scoreStates)
        {
            std::array<std::byte, protocol::v1::ScoreState::Wire::PayloadBytes> payload;
            if (protocol::v1::ScoreState::Encode(scoreState, payload) != protocol::WorldProtocolError::Success ||
                SubmitOutbound(channel,
                               psnr::core::NrPacketType{
                                   static_cast<std::uint16_t>(protocol::S2CPacketType::ScoreState),
                               },
                               psnr::runtime::NrByteView{
                                   payload.data(),
                                   static_cast<std::uint32_t>(payload.size()),
                               })
                    .Failed())
            {
                return false;
            }
        }

        std::array<std::byte, protocol::v1::RoundState::Wire::PayloadBytes> roundPayload;
        return plan.hasRoundState &&
               protocol::v1::RoundState::Encode(plan.roundState, roundPayload) ==
                   protocol::WorldProtocolError::Success &&
               SubmitOutbound(channel,
                              psnr::core::NrPacketType{
                                  static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState),
                              },
                              psnr::runtime::NrByteView{
                                  roundPayload.data(),
                                  static_cast<std::uint32_t>(roundPayload.size()),
                              })
                   .Succeeded();
    }

    bool WorldIngressEventConsumer::RecordObserverGameplayBaseline(
        const psnr::runtime::NrSessionSendChannel& channel) noexcept
    {
        if (!gameplayEnabled_)
        {
            return true;
        }

        WorldResult<WorldGameplayReplicationPlan> planResult =
            gameplayReplicationPlanner_.BuildJoinBaseline(currentServerTick_, config_.gameplay, gameplayState_);
        if (planResult.Failed())
        {
            return false;
        }
        const WorldGameplayReplicationPlan plan = planResult.TakeValue();
        std::array<std::byte, protocol::v1::RoundState::Wire::PayloadBytes> payload;
        return plan.hasRoundState &&
               protocol::v1::RoundState::Encode(plan.roundState, payload) == protocol::WorldProtocolError::Success &&
               SubmitOutbound(channel,
                              psnr::core::NrPacketType{
                                  static_cast<std::uint16_t>(protocol::S2CPacketType::RoundState),
                              },
                              psnr::runtime::NrByteView{
                                  payload.data(),
                                  static_cast<std::uint32_t>(payload.size()),
                              })
                   .Succeeded();
    }

    WorldAoiReplicationRecordResult WorldIngressEventConsumer::RecordAoiReplication(
        const std::uint32_t serverTick, const std::uint32_t replicationBatchServerTick,
        const std::span<const WorldSession> joinedSessions, const bool skipInitialRecipients,
        const AoiReplicationContent content) noexcept
    {
        if (!AoiReplicationEnabled())
        {
            return WorldAoiReplicationRecordResult::Skipped;
        }

        try
        {
            // 현재 World 상태를 spatial proxy로 변환
            WorldResult<std::vector<WorldSpatialProxy>> spatialProxiesResult =
                WorldSpatialProjectionBuilder::Build(entityManager_);
            if (spatialProxiesResult.Failed())
            {
                return WorldAoiReplicationRecordResult::ProjectionFailed;
            }
            std::vector<WorldSpatialProxy> spatialProxies = spatialProxiesResult.TakeValue();

            // spatial index 전체 rebuild
            // post-commit 된 entity 위치를 uniform grid 에 등록
            if (spatialIndex_->Rebuild(std::move(spatialProxies)) != WorldSpatialIndexBuildResult::Built)
            {
                return WorldAoiReplicationRecordResult::SpatialBuildFailed;
            }

            // 이번 AOI 계산에 참여할 recipient 구성
            std::vector<WorldAoiRecipient> recipients;
            recipients.reserve(joinedSessions.size());
            for (const WorldSession& session : joinedSessions)
            {
                if (skipInitialRecipients &&
                    std::find(initialAoiRecipientsThisTick_.begin(), initialAoiRecipientsThisTick_.end(),
                              session.sessionKey) != initialAoiRecipientsThisTick_.end())
                {
                    continue;
                }
                if (!session.IsJoined())
                {
                    return WorldAoiReplicationRecordResult::AoiPlanFailed;
                }
                if (gameplayEnabled_)
                {
                    WorldPlayerScore score;
                    if (!gameplayState_.TryFindPlayerScore(session.playerId, &score) ||
                        score.controlledEntityKey != session.entityKey ||
                        (score.lifecycle != WorldPlayerLifecycle::Alive &&
                         score.lifecycle != WorldPlayerLifecycle::SpawnPending))
                    {
                        return WorldAoiReplicationRecordResult::AoiPlanFailed;
                    }
                    if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
                    {
                        continue;
                    }
                }
                recipients.push_back(WorldAoiRecipient{session.sessionKey, session.entityKey});
            }
            std::sort(recipients.begin(), recipients.end(),
                      [](const WorldAoiRecipient& left, const WorldAoiRecipient& right) noexcept
                      { return left.sessionKey.value < right.sessionKey.value; });

            // AOI query 와 visible-set diff
            std::vector<WorldAoiVisibilityDiff> visibilityDiffs;
            if (aoiPlanner_.PlanAndCommit(recipients, *spatialIndex_, &visibilityDiffs) != WorldAoiPlanResult::Planned)
            {
                return WorldAoiReplicationRecordResult::AoiPlanFailed;
            }

            // Diff 를 replication plan으로 변환
            const bool publishRemoteEntityStates = gameplayEnabled_ && content == AoiReplicationContent::FullSnapshot;
            const bool includeV1StateRecords =
                content == AoiReplicationContent::FullSnapshot && !publishRemoteEntityStates;
            const std::span<const WorldSession> registeredSessions = sessionRegistry_.JoinedSessions();
            std::vector<WorldSession> identitySessions(registeredSessions.begin(), registeredSessions.end());
            for (const WorldSession& session : joinedSessions)
            {
                if (WorldSessionLookup::FindUniqueByPlayerId(identitySessions, session.playerId) == nullptr)
                {
                    identitySessions.push_back(session);
                }
            }
            WorldResult<WorldReplicationPlan> planResult = replicationPlanner_.Build(
                serverTick, visibilityDiffs, entityManager_, identitySessions, includeV1StateRecords);
            if (planResult.Failed())
            {
                return WorldAoiReplicationRecordResult::ReplicationPlanFailed;
            }
            WorldReplicationPlan plan = planResult.TakeValue();
            std::vector<std::vector<protocol::v2::EntityStateBatch>> remoteStateChunks;
            if (publishRemoteEntityStates)
            {
                if (nextRemoteEntitySnapshotId_ > std::numeric_limits<std::uint32_t>::max())
                {
                    return WorldAoiReplicationRecordResult::ReplicationPlanFailed;
                }
                const std::uint32_t snapshotId = static_cast<std::uint32_t>(nextRemoteEntitySnapshotId_);
                ++nextRemoteEntitySnapshotId_;
                remoteStateChunks.reserve(plan.recipients.size());
                for (const WorldReplicationRecipientPlan& recipient : plan.recipients)
                {
                    WorldResult<std::vector<protocol::v2::EntityStateBatch>> chunksResult =
                        gameplayReplicationPlanner_.BuildRemoteEntityStateChunks(
                            serverTick, snapshotId, aoiPlanner_.VisibleEntities(recipient.sessionKey), gameplayState_,
                            entityManager_);
                    if (chunksResult.Failed())
                    {
                        return WorldAoiReplicationRecordResult::ReplicationPlanFailed;
                    }
                    remoteStateChunks.push_back(chunksResult.TakeValue());
                }
            }
            // 한 outbound slot은 replication metadata tick 하나만 소유한다.
            // lifecycle payload의 발생 tick은 유지하고 slot metadata만 최종 snapshot tick으로 통일한다.
            plan.serverTick = replicationBatchServerTick;

            // recipient 를 Runtime Send Channel 에 연결
            std::vector<psnr::runtime::NrSessionSendChannel> recipientChannels;
            recipientChannels.reserve(plan.recipients.size());
            for (const WorldReplicationRecipientPlan& recipient : plan.recipients)
            {
                const SessionKeyToSendChannelMap::const_iterator channelFound =
                    sessionKeyToSendChannel_.find(recipient.sessionKey);
                if (channelFound == sessionKeyToSendChannel_.end())
                {
                    return WorldAoiReplicationRecordResult::MissingRecipientChannel;
                }
                recipientChannels.push_back(channelFound->second);
            }

            // outbound slot에 encode
            if (replicationPublisher_.Record(plan, recipientChannels, *outboundBuffer_) !=
                WorldReplicationRecordResult::Recorded)
            {
                return WorldAoiReplicationRecordResult::OutboundRejected;
            }
            for (std::size_t recipientIndex = 0; recipientIndex < remoteStateChunks.size(); ++recipientIndex)
            {
                if (replicationPublisher_.RecordRemoteEntityStateChunks(
                        remoteStateChunks[recipientIndex], recipientChannels[recipientIndex], *outboundBuffer_) !=
                    WorldReplicationRecordResult::Recorded)
                {
                    return WorldAoiReplicationRecordResult::OutboundRejected;
                }
            }
            return WorldAoiReplicationRecordResult::Recorded;
        }
        catch (...)
        {
            return WorldAoiReplicationRecordResult::OutboundRejected;
        }
    }
} // namespace psnr::world
