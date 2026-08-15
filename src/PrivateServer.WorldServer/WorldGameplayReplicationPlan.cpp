#include "pch.h"

#include "WorldGameplayReplicationPlan.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace psnr::world
{
    bool WorldGameplayReplicationPlanner::RecipientLess(const WorldSessionKey left,
                                                        const WorldSessionKey right) noexcept
    {
        return left.value < right.value;
    }

    bool WorldGameplayReplicationPlanner::EntityRemovalPlanLess(const WorldGameplayEntityRemovalPlan& left,
                                                                const WorldGameplayEntityRemovalPlan& right) noexcept
    {
        if (left.sessionKey.value != right.sessionKey.value)
        {
            return left.sessionKey.value < right.sessionKey.value;
        }
        if (left.entityRemove.entityId != right.entityRemove.entityId)
        {
            return left.entityRemove.entityId < right.entityRemove.entityId;
        }
        return left.entityRemove.generation < right.entityRemove.generation;
    }

    protocol::RoundPhase WorldGameplayReplicationPlanner::ToProtocolRoundPhase(const WorldRoundPhase phase) noexcept
    {
        switch (phase)
        {
        case WorldRoundPhase::Waiting:
            return protocol::RoundPhase::Waiting;
        case WorldRoundPhase::Running:
            return protocol::RoundPhase::Running;
        case WorldRoundPhase::Ended:
            return protocol::RoundPhase::Ended;
        default:
            return protocol::RoundPhase::Invalid;
        }
    }

    protocol::v1::RoundState WorldGameplayReplicationPlanner::MakeRoundState(
        const std::uint32_t serverTick, const WorldGameplayConfig& config,
        const WorldRoundRuntimeState& roundState) noexcept
    {
        return protocol::v1::RoundState{
            serverTick,
            roundState.roundId,
            ToProtocolRoundPhase(roundState.phase),
            roundState.phaseEndsAtServerTick,
            config.scoreToWin,
            roundState.winnerPlayerId,
        };
    }

    const WorldGameplayEntityRemoval* WorldGameplayReplicationPlanner::FindEntityRemoval(
        const std::vector<WorldGameplayEntityRemoval>& removals, const WorldEntityKey entityKey) noexcept
    {
        for (const WorldGameplayEntityRemoval& removal : removals)
        {
            if (removal.entityKey == entityKey)
            {
                return &removal;
            }
        }
        return nullptr;
    }

    protocol::EntityRemoveReason WorldGameplayReplicationPlanner::ToProtocolRemoveReason(
        const WorldGameplayEntityRemoveReason reason) noexcept
    {
        switch (reason)
        {
        case WorldGameplayEntityRemoveReason::Collected:
            return protocol::EntityRemoveReason::Collected;
        case WorldGameplayEntityRemoveReason::PlayerDeath:
            return protocol::EntityRemoveReason::Destroyed;
        case WorldGameplayEntityRemoveReason::OutsideActiveArea:
            return protocol::EntityRemoveReason::Destroyed;
        case WorldGameplayEntityRemoveReason::RoundReset:
            return protocol::EntityRemoveReason::RoundReset;
        default:
            return protocol::EntityRemoveReason::Invalid;
        }
    }

    protocol::v2::BoostState WorldGameplayReplicationPlanner::ToProtocolBoostState(const WorldBoostState state) noexcept
    {
        switch (state)
        {
        case WorldBoostState::Off:
            return protocol::v2::BoostState::Off;
        case WorldBoostState::On:
            return protocol::v2::BoostState::On;
        default:
            return protocol::v2::BoostState::Invalid;
        }
    }

    WorldResult<WorldGameplayReplicationPlan> WorldGameplayReplicationPlanner::BuildScoreBroadcast(
        const WorldGameplayCommitReport& commitReport,
        const std::span<const WorldSession> joinedSessions) const noexcept
    {
        try
        {
            WorldGameplayReplicationPlan built;
            built.serverTick = commitReport.serverTick;
            built.worldBroadcastRecipients.reserve(joinedSessions.size());
            built.scoreStates.reserve(commitReport.scoreSnapshots.size());

            // 갱신 데이터 받을 recipient (지금은 broadcast)
            for (std::size_t sessionIndex = 0; sessionIndex < joinedSessions.size(); ++sessionIndex)
            {
                const WorldSession& session = joinedSessions[sessionIndex];
                if (!session.sessionKey.IsValid() || !session.IsJoined())
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                for (std::size_t previousIndex = 0; previousIndex < sessionIndex; ++previousIndex)
                {
                    const WorldSession& previous = joinedSessions[previousIndex];
                    if (previous.sessionKey == session.sessionKey || previous.playerId == session.playerId ||
                        previous.entityKey == session.entityKey)
                    {
                        return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                    }
                }
                built.worldBroadcastRecipients.push_back(session.sessionKey);
            }
            std::sort(built.worldBroadcastRecipients.begin(), built.worldBroadcastRecipients.end(), RecipientLess);
            for (std::size_t index = 1; index < built.worldBroadcastRecipients.size(); ++index)
            {
                if (built.worldBroadcastRecipients[index - 1] == built.worldBroadcastRecipients[index])
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
            }

            for (std::size_t index = 0; index < commitReport.scoreSnapshots.size(); ++index)
            {
                const WorldGameplayScoreSnapshot& snapshot = commitReport.scoreSnapshots[index];
                if (snapshot.playerId == 0 || !snapshot.controlledEntityKey.IsValid() ||
                    (index > 0 && commitReport.scoreSnapshots[index - 1].playerId >= snapshot.playerId))
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }

                const WorldSession* const session =
                    WorldSessionLookup::FindFirstByPlayerId(joinedSessions, snapshot.playerId);
                if (session == nullptr || session->entityKey != snapshot.controlledEntityKey)
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                built.scoreStates.push_back(
                    protocol::v1::ScoreState{commitReport.serverTick, snapshot.playerId, snapshot.score});
            }

            return WorldResult<WorldGameplayReplicationPlan>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    // 한 tick 의 commit 결과를 전체 recipient 에게 broadcast 할 수 있게 변환
    WorldResult<WorldGameplayReplicationPlan> WorldGameplayReplicationPlanner::BuildBroadcast(
        const WorldGameplayConfig& config, const WorldGameplayCommitReport& commitReport,
        const std::span<const WorldSession> joinedSessions) const noexcept
    {
        if (config.scoreToWin == 0)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
        }

        WorldResult<WorldGameplayReplicationPlan> scoreResult = BuildScoreBroadcast(commitReport, joinedSessions);
        if (scoreResult.Failed())
        {
            return scoreResult;
        }
        WorldGameplayReplicationPlan built = scoreResult.TakeValue();

        if (commitReport.roundSnapshotChanged)
        {
            if (commitReport.roundSnapshot.phase == WorldRoundPhase::Ended)
            {
                return WorldResult<WorldGameplayReplicationPlan>(std::move(built));
            }
            const protocol::v1::RoundState roundState =
                MakeRoundState(commitReport.serverTick, config, commitReport.roundSnapshot);
            if (roundState.phase == protocol::RoundPhase::Invalid || roundState.roundId == 0 ||
                (roundState.phase == protocol::RoundPhase::Ended && roundState.winnerPlayerId == 0) ||
                (roundState.phase != protocol::RoundPhase::Ended && roundState.winnerPlayerId != 0))
            {
                return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
            }
            built.hasRoundState = true;
            built.roundState = roundState;
        }

        return WorldResult<WorldGameplayReplicationPlan>(std::move(built));
    }

    // 신규 접속한 player 에게 현재 gameplay 상태 전체를 알려주기 위한 baseline 생성
    WorldResult<WorldGameplayReplicationPlan> WorldGameplayReplicationPlanner::BuildJoinBaseline(
        const std::uint32_t serverTick, const WorldGameplayConfig& config,
        const WorldGameplayState& gameplayState) const noexcept
    {
        if (config.scoreToWin == 0)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            WorldGameplayReplicationPlan built;
            built.serverTick = serverTick;
            built.scoreStates.reserve(gameplayState.PlayerScores().size());
            built.joinPacketOrder.reserve(gameplayState.PlayerScores().size() + 3);
            built.joinPacketOrder.push_back(WorldGameplayJoinPacketKind::ControlledEntitySpawn);
            for (std::size_t index = 0; index < gameplayState.PlayerScores().size(); ++index)
            {
                const WorldPlayerScore& score = gameplayState.PlayerScores()[index];
                if (score.playerId == 0 || !score.controlledEntityKey.IsValid() ||
                    (index > 0 && gameplayState.PlayerScores()[index - 1].playerId >= score.playerId))
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                built.scoreStates.push_back(protocol::v1::ScoreState{serverTick, score.playerId, score.score});
                built.joinPacketOrder.push_back(WorldGameplayJoinPacketKind::ScoreState);
            }

            built.roundState = MakeRoundState(serverTick, config, gameplayState.RoundState());
            if (built.roundState.phase == protocol::RoundPhase::Invalid || built.roundState.roundId == 0 ||
                (built.roundState.phase == protocol::RoundPhase::Ended && built.roundState.winnerPlayerId == 0) ||
                (built.roundState.phase != protocol::RoundPhase::Ended && built.roundState.winnerPlayerId != 0))
            {
                return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
            }
            built.hasRoundState = true;
            built.joinPacketOrder.push_back(WorldGameplayJoinPacketKind::RoundState);
            built.joinPacketOrder.push_back(WorldGameplayJoinPacketKind::WorldReady);
            return WorldResult<WorldGameplayReplicationPlan>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    // 삭제된 entity 를 이전 AOI observer 와 사망한 controlled entity owner 에게 알리기 위한 plan
    WorldResult<WorldGameplayReplicationPlan> WorldGameplayReplicationPlanner::BuildEntityRemovals(
        const WorldGameplayCommitReport& commitReport,
        const std::span<const WorldAoiPrunedVisibility> prunedVisibilities,
        const std::span<const WorldSession> joinedSessions) const noexcept
    {
        try
        {
            WorldGameplayReplicationPlan built;
            built.serverTick = commitReport.serverTick;
            built.entityRemovals.reserve(prunedVisibilities.size() + commitReport.entityRemovals.size());
            for (std::size_t index = 0; index < prunedVisibilities.size(); ++index)
            {
                const WorldAoiPrunedVisibility& pruned = prunedVisibilities[index];
                if (!pruned.sessionKey.IsValid() || !pruned.entityKey.IsValid() ||
                    (index > 0 && (prunedVisibilities[index - 1].sessionKey.value > pruned.sessionKey.value ||
                                   (prunedVisibilities[index - 1].sessionKey == pruned.sessionKey &&
                                    !(prunedVisibilities[index - 1].entityKey < pruned.entityKey)))))
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }

                const WorldGameplayEntityRemoval* const removal =
                    FindEntityRemoval(commitReport.entityRemovals, pruned.entityKey);
                if (removal == nullptr)
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                const protocol::EntityRemoveReason reason = ToProtocolRemoveReason(removal->reason);
                if (reason == protocol::EntityRemoveReason::Invalid)
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                built.entityRemovals.push_back(WorldGameplayEntityRemovalPlan{
                    pruned.sessionKey,
                    protocol::v1::EntityRemove{commitReport.serverTick, pruned.entityKey.entityId,
                                               pruned.entityKey.generation, reason},
                });
            }

            for (const WorldGameplayEntityRemoval& removal : commitReport.entityRemovals)
            {
                if (removal.reason != WorldGameplayEntityRemoveReason::PlayerDeath)
                {
                    continue;
                }

                const WorldSession* ownerSession = nullptr;
                for (const WorldSession& session : joinedSessions)
                {
                    if (!session.IsJoined())
                    {
                        return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                    }
                    if (session.entityKey == removal.entityKey)
                    {
                        if (ownerSession != nullptr)
                        {
                            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                        }
                        ownerSession = &session;
                    }
                }
                if (ownerSession == nullptr)
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }

                built.entityRemovals.push_back(WorldGameplayEntityRemovalPlan{
                    ownerSession->sessionKey,
                    protocol::v1::EntityRemove{commitReport.serverTick, removal.entityKey.entityId,
                                               removal.entityKey.generation, protocol::EntityRemoveReason::Destroyed},
                });
            }

            std::sort(built.entityRemovals.begin(), built.entityRemovals.end(), EntityRemovalPlanLess);
            built.entityRemovals.erase(std::unique(built.entityRemovals.begin(), built.entityRemovals.end()),
                                       built.entityRemovals.end());

            return WorldResult<WorldGameplayReplicationPlan>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<protocol::v2::ControlledEntityState> WorldGameplayReplicationPlanner::BuildControlledEntityState(
        const std::uint32_t serverTick, const WorldSession& session, const WorldPlayerScore& score,
        const WorldEntityComponents& components) const noexcept
    {
        if (session.playerId != score.playerId || session.entityKey != score.controlledEntityKey ||
            components.playerControl.playerId != session.playerId)
        {
            return WorldResult<protocol::v2::ControlledEntityState>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            const std::size_t bodySampleCount = components.bodyTrail.SampleCount();
            protocol::v2::ControlledEntityState built;
            built.serverTick = serverTick;
            built.controlledEntityGeneration = session.entityKey.generation;
            built.lastProcessedControlSequence = components.playerControl.lastInputSequence;
            built.headPositionX = components.transform.positionX;
            built.headPositionY = components.transform.positionY;
            built.headingRadians = components.transform.angleRadians;
            built.diameter = components.replicationMetadata.primaryCircleRadius * 2.0f;
            built.growthPoint = score.score;
            built.boostState = ToProtocolBoostState(components.playerControl.boostState);
            built.bodyTrailSamples.reserve(bodySampleCount);
            for (std::size_t index = 0; index < bodySampleCount; ++index)
            {
                BodyTrailSample sample;
                if (!components.bodyTrail.TryRead(index, &sample))
                {
                    return WorldResult<protocol::v2::ControlledEntityState>::Failure(WorldErrorCode::InvalidInput);
                }
                built.bodyTrailSamples.push_back(
                    protocol::v2::ControlledEntityBodySample{sample.positionX, sample.positionY});
            }

            return WorldResult<protocol::v2::ControlledEntityState>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<protocol::v2::ControlledEntityState>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<std::vector<protocol::v2::EntityStateBatch>> WorldGameplayReplicationPlanner::
        BuildRemoteEntityStateChunks(const std::uint32_t serverTick, const std::uint32_t snapshotId,
                                     const std::span<const WorldEntityKey> visibleEntityKeys,
                                     const WorldGameplayState& gameplayState,
                                     const WorldEntityManager& entityManager) const noexcept
    {
        if (snapshotId == 0)
        {
            return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            std::vector<protocol::v2::EntityStateBatch> chunks;
            protocol::v2::EntityStateBatch currentChunk;
            currentChunk.serverTick = serverTick;
            currentChunk.snapshotId = snapshotId;
            std::size_t currentPayloadBytes = protocol::v2::EntityStateBatch::Wire::HeaderBytes;

            for (const WorldEntityKey entityKey : visibleEntityKeys)
            {
                EntityHandle handle;
                const WorldEntityComponents* components = nullptr;
                if (!entityManager.TryFindHandle(entityKey, &handle) ||
                    !entityManager.TryReadComponentsView(handle, &components))
                {
                    return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(
                        WorldErrorCode::InvalidInput);
                }
                if (components->replicationMetadata.entityKind != WorldEntityKind::Player)
                {
                    continue;
                }

                WorldPlayerScore score;
                if (!gameplayState.TryFindPlayerScore(components->playerControl.playerId, &score) ||
                    score.lifecycle != WorldPlayerLifecycle::Alive || score.controlledEntityKey != entityKey ||
                    components->bodyTrail.SampleCount() == 0)
                {
                    return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(
                        WorldErrorCode::InvalidInput);
                }

                protocol::v2::EntityStateRecord record;
                record.entityId = entityKey.entityId;
                record.generation = entityKey.generation;
                record.headPositionX = components->transform.positionX;
                record.headPositionY = components->transform.positionY;
                record.headingRadians = components->transform.angleRadians;
                record.diameter = components->replicationMetadata.primaryCircleRadius * 2.0f;
                record.growthPoint = score.score;
                record.boostState = ToProtocolBoostState(components->playerControl.boostState);
                record.bodyTrailSamples.reserve(components->bodyTrail.SampleCount());
                for (std::size_t sampleIndex = 0; sampleIndex < components->bodyTrail.SampleCount(); ++sampleIndex)
                {
                    BodyTrailSample sample;
                    if (!components->bodyTrail.TryRead(sampleIndex, &sample))
                    {
                        return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(
                            WorldErrorCode::InvalidInput);
                    }
                    record.bodyTrailSamples.push_back(
                        protocol::v2::EntityStateBodySample{sample.positionX, sample.positionY});
                }

                const std::size_t recordBytes =
                    protocol::v2::EntityStateRecord::Wire::HeaderBytes +
                    record.bodyTrailSamples.size() * protocol::v2::EntityStateRecord::Wire::BodySampleBytes;
                if (recordBytes > protocol::v2::EntityStateBatch::Wire::MaximumPayloadBytes -
                                      protocol::v2::EntityStateBatch::Wire::HeaderBytes)
                {
                    return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(
                        WorldErrorCode::InvalidInput);
                }
                if (!currentChunk.records.empty() &&
                    recordBytes > protocol::v2::EntityStateBatch::Wire::MaximumPayloadBytes - currentPayloadBytes)
                {
                    chunks.push_back(std::move(currentChunk));
                    currentChunk = protocol::v2::EntityStateBatch{};
                    currentChunk.serverTick = serverTick;
                    currentChunk.snapshotId = snapshotId;
                    currentPayloadBytes = protocol::v2::EntityStateBatch::Wire::HeaderBytes;
                }
                currentPayloadBytes += recordBytes;
                currentChunk.records.push_back(std::move(record));
            }
            if (!currentChunk.records.empty())
            {
                chunks.push_back(std::move(currentChunk));
            }
            if (chunks.size() > std::numeric_limits<std::uint16_t>::max())
            {
                return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(WorldErrorCode::InvalidInput);
            }
            const std::uint16_t chunkCount = static_cast<std::uint16_t>(chunks.size());
            for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex)
            {
                chunks[chunkIndex].chunkIndex = static_cast<std::uint16_t>(chunkIndex);
                chunks[chunkIndex].chunkCount = chunkCount;
            }

            return WorldResult<std::vector<protocol::v2::EntityStateBatch>>(std::move(chunks));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::vector<protocol::v2::EntityStateBatch>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<WorldOverviewPlanInput> WorldGameplayReplicationPlanner::BuildOverviewInput(
        const std::uint32_t serverTick, const std::uint32_t overviewId, const WorldPhysicsArenaBounds& mapBounds,
        const WorldActiveArea& activeArea, const WorldGameplayState& gameplayState,
        const WorldEntityManager& entityManager) const noexcept
    {
        if (overviewId == 0)
        {
            return WorldResult<WorldOverviewPlanInput>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            WorldOverviewPlanInput built;
            built.serverTick = serverTick;
            built.overviewId = overviewId;
            built.mapMinX = mapBounds.minimumX;
            built.mapMinY = mapBounds.minimumY;
            built.mapMaxX = mapBounds.maximumX;
            built.mapMaxY = mapBounds.maximumY;
            built.activeAreaCenterX = activeArea.centerX;
            built.activeAreaCenterY = activeArea.centerY;
            built.activeAreaRadius = activeArea.radius;
            built.alivePlayers.reserve(gameplayState.PlayerScores().size());

            for (const WorldPlayerScore& score : gameplayState.PlayerScores())
            {
                if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
                {
                    continue;
                }
                if (score.lifecycle != WorldPlayerLifecycle::Alive)
                {
                    return WorldResult<WorldOverviewPlanInput>::Failure(WorldErrorCode::InvalidInput);
                }

                EntityHandle handle;
                const WorldEntityComponents* components = nullptr;
                if (!entityManager.TryFindHandle(score.controlledEntityKey, &handle) ||
                    !entityManager.TryReadComponentsView(handle, &components) || components == nullptr ||
                    components->replicationMetadata.entityKind != WorldEntityKind::Player ||
                    components->playerControl.playerId != score.playerId || components->bodyTrail.SampleCount() == 0)
                {
                    return WorldResult<WorldOverviewPlanInput>::Failure(WorldErrorCode::InvalidInput);
                }

                WorldOverviewPlayerInput player;
                player.playerId = score.playerId;
                player.growthPoint = score.score;
                player.bodySamples.reserve(components->bodyTrail.SampleCount());
                for (std::size_t index = 0; index < components->bodyTrail.SampleCount(); ++index)
                {
                    BodyTrailSample sample;
                    if (!components->bodyTrail.TryRead(index, &sample))
                    {
                        return WorldResult<WorldOverviewPlanInput>::Failure(WorldErrorCode::InvalidInput);
                    }
                    player.bodySamples.push_back(protocol::v2::WorldOverviewPoint{sample.positionX, sample.positionY});
                }
                built.alivePlayers.push_back(std::move(player));
            }

            return WorldResult<WorldOverviewPlanInput>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldOverviewPlanInput>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<WorldGameplayReplicationPlan> WorldGameplayReplicationPlanner::BuildRoundResults(
        const std::uint32_t endTick, const std::uint32_t roundId, const std::span<const WorldPlayerScore> playerScores,
        const std::span<const WorldSession> joinedSessions) const noexcept
    {
        if (endTick == 0 || roundId == 0)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            std::vector<WorldRoundResultPlayerInput> playerInputs;
            playerInputs.reserve(playerScores.size());
            for (const WorldPlayerScore& score : playerScores)
            {
                playerInputs.push_back(WorldRoundResultPlayerInput{
                    score.playerId,
                    score.score,
                    score.lifecycle,
                    WorldSessionLookup::FindFirstByPlayerId(joinedSessions, score.playerId) != nullptr,
                });
            }

            const WorldRoundResultPlanner winnerPlanner;
            WorldResult<WorldRoundResultPlan> winnerResult = winnerPlanner.Plan(playerInputs);
            if (winnerResult.Failed() && winnerResult.Error() == WorldErrorCode::AllocationFailed)
            {
                return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::AllocationFailed);
            }
            if (winnerResult.Failed())
            {
                return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
            }
            const WorldRoundResultPlan winnerPlan = winnerResult.TakeValue();
            if (winnerPlan.winnerPlayerIds.size() > protocol::v2::RoundResult::Wire::MaximumWinnerCount)
            {
                return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
            }

            WorldGameplayReplicationPlan built;
            built.serverTick = endTick;
            built.roundResults.reserve(joinedSessions.size());
            for (const WorldSession& session : joinedSessions)
            {
                const WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindByPlayerId(playerScores, session.playerId);
                if (!session.sessionKey.IsValid() || !session.IsJoined() || score == nullptr)
                {
                    return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }

                built.roundResults.push_back(WorldGameplayRoundResultPlan{
                    session.sessionKey,
                    protocol::v2::RoundResult{
                        endTick,
                        roundId,
                        winnerPlan.winningGrowthPoint,
                        score->score,
                        winnerPlan.winnerPlayerIds,
                    },
                });
            }
            std::sort(built.roundResults.begin(), built.roundResults.end(),
                      [](const WorldGameplayRoundResultPlan& left, const WorldGameplayRoundResultPlan& right) noexcept
                      { return left.sessionKey.value < right.sessionKey.value; });

            return WorldResult<WorldGameplayReplicationPlan>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldGameplayReplicationPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
