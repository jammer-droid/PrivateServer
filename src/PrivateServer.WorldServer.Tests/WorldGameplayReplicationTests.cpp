#include "pch.h"

#include "WorldGameplayReplicationPlan.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] bool InitializeBodyTrail(const std::uint32_t maxSampleCount,
                                               BodyTrailComponent* const outBodyTrail)
        {
            WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(maxSampleCount);
            if (outBodyTrail == nullptr || result.Failed())
            {
                return false;
            }
            *outBodyTrail = result.TakeValue();
            return true;
        }

        [[nodiscard]] WorldGameplayConfig MakeGameplayConfig()
        {
            WorldGameplayConfig config;
            config.minimumPlayersToStart = 2;
            config.scoreToWin = 5;
            config.roundDurationTicks = 1200;
            config.endedDurationTicks = 60;
            config.resourceArchetypeId = 2;
            config.resourceCircleRadius = 0.5f;
            config.resourceScoreValue = 1;
            config.resourceDensityPerUnit2 = 0.000032f;
            return config;
        }

        [[nodiscard]] WorldGameplayState MakeGameplayState(const WorldGameplayConfig& config,
                                                           const WorldPhysicsArenaBounds& arenaBounds)
        {
            WorldResult<WorldGameplayState> result = CreateWorldGameplayState(config, arenaBounds);
            EXPECT_TRUE(result.Succeeded());
            return result.TakeValue();
        }

        [[nodiscard]] WorldEntityComponents MakeRemotePlayerComponents(const std::uint32_t playerId,
                                                                       const float headPositionX,
                                                                       const std::size_t bodySampleCount)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{headPositionX, 2.0f, 0.5f};
            components.replicationMetadata.entityKind = WorldEntityKind::Player;
            components.replicationMetadata.primaryCircleRadius = 0.75f;
            components.playerControl.playerId = playerId;
            components.playerControl.boostState = WorldBoostState::On;
            EXPECT_LE(bodySampleCount, static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()));
            EXPECT_TRUE(InitializeBodyTrail(static_cast<std::uint32_t>(bodySampleCount), &components.bodyTrail));
            for (std::size_t index = 0; index < bodySampleCount; ++index)
            {
                EXPECT_TRUE(
                    components.bodyTrail.TryPushBack(BodyTrailSample{headPositionX - static_cast<float>(index), 2.0f}));
            }
            return components;
        }
    } // namespace

    TEST(WorldGameplayReplicationTests, PlansStableFullScoreBroadcastForEveryJoinedSession)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 50;
        report.scoreSnapshots = {
            WorldGameplayScoreSnapshot{10, WorldEntityKey{1, 1}, 3},
            WorldGameplayScoreSnapshot{20, WorldEntityKey{2, 1}, 5},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{7}, 20, WorldEntityKey{2, 1}},
            WorldSession{WorldSessionKey{2}, 10, WorldEntityKey{1, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildScoreBroadcast(report, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        EXPECT_EQ(plan.serverTick, 50u);
        ASSERT_EQ(plan.worldBroadcastRecipients.size(), 2u);
        EXPECT_EQ(plan.worldBroadcastRecipients[0], WorldSessionKey{2});
        EXPECT_EQ(plan.worldBroadcastRecipients[1], WorldSessionKey{7});
        ASSERT_EQ(plan.scoreStates.size(), 2u);
        EXPECT_EQ(plan.scoreStates[0], (protocol::v1::ScoreState{50, 10, 3}));
        EXPECT_EQ(plan.scoreStates[1], (protocol::v1::ScoreState{50, 20, 5}));
    }

    TEST(WorldGameplayReplicationTests, BroadcastsChangedScoreToUnchangedJoinedPlayers)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 50;
        report.scoreSnapshots = {
            WorldGameplayScoreSnapshot{10, WorldEntityKey{1, 1}, 3},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{7}, 20, WorldEntityKey{2, 1}},
            WorldSession{WorldSessionKey{2}, 10, WorldEntityKey{1, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildScoreBroadcast(report, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        EXPECT_EQ(plan.worldBroadcastRecipients.size(), 2u);
        ASSERT_EQ(plan.scoreStates.size(), 1u);
        EXPECT_EQ(plan.scoreStates[0], (protocol::v1::ScoreState{50, 10, 3}));
    }

    TEST(WorldGameplayReplicationTests, RejectsScoreSnapshotThatDoesNotMatchJoinedEntity)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 50;
        report.scoreSnapshots = {
            WorldGameplayScoreSnapshot{10, WorldEntityKey{1, 2}, 3},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{2}, 10, WorldEntityKey{1, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        const WorldResult<WorldGameplayReplicationPlan> result = planner.BuildScoreBroadcast(report, joinedSessions);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }

    TEST(WorldGameplayReplicationTests, OmitsLegacyRoundStateWhenRoundEnds)
    {
        const WorldGameplayConfig config = MakeGameplayConfig();
        WorldGameplayCommitReport report;
        report.serverTick = 50;
        report.scoreSnapshots = {
            WorldGameplayScoreSnapshot{10, WorldEntityKey{1, 1}, 5},
        };
        report.roundSnapshotChanged = true;
        report.roundSnapshot = WorldRoundRuntimeState{1, WorldRoundPhase::Ended, 110, 0};
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{2}, 10, WorldEntityKey{1, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildBroadcast(config, report, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        ASSERT_EQ(plan.scoreStates.size(), 1u);
        EXPECT_FALSE(plan.hasRoundState);
    }

    TEST(WorldGameplayReplicationTests, JoinBaselineOrdersScoresAndRoundBeforeWorldReady)
    {
        const WorldGameplayConfig config = MakeGameplayConfig();
        WorldGameplayState state = MakeGameplayState(config, WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f});
        ASSERT_EQ(state.TryRegisterPlayer(20, WorldEntityKey{2, 1}), WorldPlayerScoreRegisterResult::Registered);
        ASSERT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::Registered);
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildJoinBaseline(50, config, state);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        ASSERT_EQ(plan.scoreStates.size(), 2u);
        EXPECT_EQ(plan.scoreStates[0].playerId, 10u);
        EXPECT_EQ(plan.scoreStates[1].playerId, 20u);
        EXPECT_TRUE(plan.hasRoundState);
        EXPECT_EQ(plan.roundState.phase, protocol::RoundPhase::Waiting);
        EXPECT_EQ(plan.joinPacketOrder, (std::vector<WorldGameplayJoinPacketKind>{
                                            WorldGameplayJoinPacketKind::ControlledEntitySpawn,
                                            WorldGameplayJoinPacketKind::ScoreState,
                                            WorldGameplayJoinPacketKind::ScoreState,
                                            WorldGameplayJoinPacketKind::RoundState,
                                            WorldGameplayJoinPacketKind::WorldReady,
                                        }));
        EXPECT_EQ(plan.joinPacketOrder.back(), WorldGameplayJoinPacketKind::WorldReady);
    }

    TEST(WorldGameplayReplicationTests, MapsPrunedRoundResetRemovalToPreviousObservers)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 160;
        report.entityRemovals = {
            WorldGameplayEntityRemoval{WorldEntityKey{7, 2}, WorldGameplayEntityRemoveReason::RoundReset},
        };
        const WorldAoiPrunedVisibility pruned[] = {
            WorldAoiPrunedVisibility{WorldSessionKey{3}, WorldEntityKey{7, 2}},
            WorldAoiPrunedVisibility{WorldSessionKey{9}, WorldEntityKey{7, 2}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildEntityRemovals(report, pruned, {});
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        ASSERT_EQ(plan.entityRemovals.size(), 2u);
        EXPECT_EQ(plan.entityRemovals[0],
                  (WorldGameplayEntityRemovalPlan{
                      WorldSessionKey{3},
                      protocol::v1::EntityRemove{160, 7, 2, protocol::EntityRemoveReason::RoundReset},
                  }));
        EXPECT_EQ(plan.entityRemovals[1].sessionKey, WorldSessionKey{9});
        EXPECT_EQ(plan.entityRemovals[1].entityRemove.reason, protocol::EntityRemoveReason::RoundReset);
    }

    TEST(WorldGameplayReplicationTests, MapsOutsideActiveAreaRemovalToDestroyed)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 50;
        report.entityRemovals = {
            WorldGameplayEntityRemoval{WorldEntityKey{7, 2}, WorldGameplayEntityRemoveReason::OutsideActiveArea},
        };
        const WorldAoiPrunedVisibility pruned[] = {
            WorldAoiPrunedVisibility{WorldSessionKey{3}, WorldEntityKey{7, 2}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildEntityRemovals(report, pruned, {});
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        ASSERT_EQ(plan.entityRemovals.size(), 1u);
        EXPECT_EQ(plan.entityRemovals[0].entityRemove.reason, protocol::EntityRemoveReason::Destroyed);
    }

    TEST(WorldGameplayReplicationTests, AddsControlledOwnerToPlayerDeathRemovalRecipients)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 70;
        report.entityRemovals = {
            WorldGameplayEntityRemoval{WorldEntityKey{7, 2}, WorldGameplayEntityRemoveReason::PlayerDeath},
        };
        const WorldAoiPrunedVisibility pruned[] = {
            WorldAoiPrunedVisibility{WorldSessionKey{9}, WorldEntityKey{7, 2}},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{3}, 10, WorldEntityKey{7, 2}},
            WorldSession{WorldSessionKey{9}, 20, WorldEntityKey{8, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildEntityRemovals(report, pruned, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        ASSERT_EQ(plan.entityRemovals.size(), 2u);
        EXPECT_EQ(plan.entityRemovals[0].sessionKey, WorldSessionKey{3});
        EXPECT_EQ(plan.entityRemovals[1].sessionKey, WorldSessionKey{9});
        EXPECT_EQ(plan.entityRemovals[0].entityRemove.reason, protocol::EntityRemoveReason::Destroyed);
    }

    TEST(WorldGameplayReplicationTests, DoesNotDuplicateControlledOwnerAlreadyInPrunedRecipients)
    {
        WorldGameplayCommitReport report;
        report.serverTick = 70;
        report.entityRemovals = {
            WorldGameplayEntityRemoval{WorldEntityKey{7, 2}, WorldGameplayEntityRemoveReason::PlayerDeath},
        };
        const WorldAoiPrunedVisibility pruned[] = {
            WorldAoiPrunedVisibility{WorldSessionKey{3}, WorldEntityKey{7, 2}},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{3}, 10, WorldEntityKey{7, 2}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildEntityRemovals(report, pruned, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        ASSERT_EQ(plan.entityRemovals.size(), 1u);
        EXPECT_EQ(plan.entityRemovals[0].sessionKey, WorldSessionKey{3});
    }

    TEST(WorldGameplayReplicationTests, ProjectsStableCoWinnersAndRecipientFinalGrowth)
    {
        const WorldPlayerScore scores[] = {
            WorldPlayerScore{10, WorldEntityKey{1, 1}, 12, 0.0f, WorldPlayerLifecycle::Alive},
            WorldPlayerScore{20, WorldEntityKey{2, 1}, 12, 0.0f, WorldPlayerLifecycle::Alive},
            WorldPlayerScore{30, WorldEntityKey{3, 1}, 99, 0.0f, WorldPlayerLifecycle::SpawnPending},
            WorldPlayerScore{40, WorldEntityKey{4, 1}, 100, 0.0f, WorldPlayerLifecycle::Alive},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{7}, 30, WorldEntityKey{3, 1}},
            WorldSession{WorldSessionKey{5}, 20, WorldEntityKey{2, 1}},
            WorldSession{WorldSessionKey{3}, 10, WorldEntityKey{1, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildRoundResults(180, 4, scores, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();

        EXPECT_EQ(plan.serverTick, 180u);
        ASSERT_EQ(plan.roundResults.size(), 3u);
        EXPECT_EQ(plan.roundResults[0].sessionKey, WorldSessionKey{3});
        EXPECT_EQ(plan.roundResults[1].sessionKey, WorldSessionKey{5});
        EXPECT_EQ(plan.roundResults[2].sessionKey, WorldSessionKey{7});
        EXPECT_EQ(plan.roundResults[0].roundResult, (protocol::v2::RoundResult{180, 4, 12, 12, {10, 20}}));
        EXPECT_EQ(plan.roundResults[1].roundResult, (protocol::v2::RoundResult{180, 4, 12, 12, {10, 20}}));
        EXPECT_EQ(plan.roundResults[2].roundResult, (protocol::v2::RoundResult{180, 4, 12, 99, {10, 20}}));
    }

    TEST(WorldGameplayReplicationTests, ProjectsNoWinnerResultForSpawnPendingRecipient)
    {
        const WorldPlayerScore scores[] = {
            WorldPlayerScore{10, WorldEntityKey{1, 1}, 7, 0.0f, WorldPlayerLifecycle::SpawnPending},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{3}, 10, WorldEntityKey{1, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldGameplayReplicationPlan> result = planner.BuildRoundResults(180, 4, scores, joinedSessions);
        ASSERT_TRUE(result.Succeeded());
        const WorldGameplayReplicationPlan plan = result.TakeValue();
        ASSERT_EQ(plan.roundResults.size(), 1u);
        EXPECT_EQ(plan.roundResults[0].roundResult, (protocol::v2::RoundResult{180, 4, 0, 7, {}}));
    }

    TEST(WorldGameplayReplicationTests, RejectsRoundResultRecipientWithoutScore)
    {
        const WorldPlayerScore scores[] = {
            WorldPlayerScore{10, WorldEntityKey{1, 1}, 7, 0.0f, WorldPlayerLifecycle::Alive},
        };
        const WorldSession joinedSessions[] = {
            WorldSession{WorldSessionKey{3}, 20, WorldEntityKey{2, 1}},
        };
        const WorldGameplayReplicationPlanner planner;
        const WorldResult<WorldGameplayReplicationPlan> result =
            planner.BuildRoundResults(180, 4, scores, joinedSessions);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }

    TEST(WorldGameplayReplicationTests, ProjectsCanonicalPlayerStateToOwnedV2SelfSnapshot)
    {
        constexpr WorldEntityKey EntityKey{7, 3};
        const WorldSession session{WorldSessionKey{2}, 10, EntityKey};
        const WorldPlayerScore score{10, EntityKey, 5, 0.0f, WorldPlayerLifecycle::Alive};
        WorldEntityComponents components;
        components.transform = TransformComponent{10.0f, 20.0f, 1.25f};
        components.replicationMetadata.entityKind = WorldEntityKind::Player;
        components.replicationMetadata.primaryCircleRadius = 0.75f;
        components.playerControl.playerId = 10;
        components.playerControl.lastInputSequence = 42;
        components.playerControl.boostState = WorldBoostState::On;
        ASSERT_TRUE(InitializeBodyTrail(3, &components.bodyTrail));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{10.0f, 20.0f}));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{9.0f, 20.0f}));

        const WorldGameplayReplicationPlanner planner;
        WorldResult<protocol::v2::ControlledEntityState> result =
            planner.BuildControlledEntityState(100, session, score, components);
        ASSERT_TRUE(result.Succeeded());
        const protocol::v2::ControlledEntityState state = result.TakeValue();

        EXPECT_EQ(state.serverTick, 100u);
        EXPECT_EQ(state.controlledEntityGeneration, 3u);
        EXPECT_EQ(state.lastProcessedControlSequence, 42u);
        EXPECT_FLOAT_EQ(state.headPositionX, 10.0f);
        EXPECT_FLOAT_EQ(state.headPositionY, 20.0f);
        EXPECT_FLOAT_EQ(state.headingRadians, 1.25f);
        EXPECT_FLOAT_EQ(state.diameter, 1.5f);
        EXPECT_EQ(state.growthPoint, 5u);
        EXPECT_EQ(state.boostState, protocol::v2::BoostState::On);
        EXPECT_EQ(state.bodyTrailSamples, (std::vector<protocol::v2::ControlledEntityBodySample>{
                                              {10.0f, 20.0f},
                                              {9.0f, 20.0f},
                                          }));
        std::vector<std::byte> payload(
            protocol::v2::ControlledEntityState::Wire::CalculatePayloadBytes(state.bodyTrailSamples.size()));
        EXPECT_EQ(protocol::v2::ControlledEntityState::Encode(state, payload), protocol::WorldProtocolError::Success);

        ASSERT_TRUE(components.bodyTrail.TryWrite(0, BodyTrailSample{100.0f, 200.0f}));
        EXPECT_EQ(state.bodyTrailSamples[0], (protocol::v2::ControlledEntityBodySample{10.0f, 20.0f}));
    }

    TEST(WorldGameplayReplicationTests, RejectsInconsistentSelfSnapshot)
    {
        constexpr WorldEntityKey EntityKey{7, 3};
        const WorldSession session{WorldSessionKey{2}, 10, EntityKey};
        const WorldPlayerScore mismatchedScore{11, EntityKey, 5, 0.0f, WorldPlayerLifecycle::Alive};
        WorldEntityComponents components;
        components.transform = TransformComponent{10.0f, 20.0f, 1.25f};
        components.replicationMetadata.entityKind = WorldEntityKind::Player;
        components.replicationMetadata.primaryCircleRadius = 0.75f;
        components.playerControl.playerId = 10;
        ASSERT_TRUE(InitializeBodyTrail(1, &components.bodyTrail));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{10.0f, 20.0f}));
        const WorldGameplayReplicationPlanner planner;
        const WorldResult<protocol::v2::ControlledEntityState> result =
            planner.BuildControlledEntityState(100, session, mismatchedScore, components);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }

    TEST(WorldGameplayReplicationTests, PlansRemotePlayersAsStableAtomicWholeSnakeChunks)
    {
        const WorldGameplayConfig config = MakeGameplayConfig();
        WorldGameplayState gameplayState =
            MakeGameplayState(config, WorldPhysicsArenaBounds{-1000.0f, -1000.0f, 1000.0f, 1000.0f});
        WorldEntityManager entityManager;

        WorldEntityKey firstPlayerKey;
        EntityHandle firstPlayerHandle;
        ASSERT_TRUE(
            entityManager.TryCreate(MakeRemotePlayerComponents(10, 10.0f, 510), &firstPlayerKey, &firstPlayerHandle));
        WorldEntityKey secondPlayerKey;
        EntityHandle secondPlayerHandle;
        ASSERT_TRUE(
            entityManager.TryCreate(MakeRemotePlayerComponents(20, 20.0f, 510), &secondPlayerKey, &secondPlayerHandle));
        WorldEntityComponents resource;
        resource.replicationMetadata.entityKind = WorldEntityKind::Resource;
        WorldEntityKey resourceKey;
        EntityHandle resourceHandle;
        ASSERT_TRUE(entityManager.TryCreate(resource, &resourceKey, &resourceHandle));
        ASSERT_EQ(gameplayState.TryRegisterPlayer(10, firstPlayerKey), WorldPlayerScoreRegisterResult::Registered);
        ASSERT_EQ(gameplayState.TryRegisterPlayer(20, secondPlayerKey), WorldPlayerScoreRegisterResult::Registered);

        const std::vector<WorldEntityKey> visibleEntityKeys{
            firstPlayerKey,
            secondPlayerKey,
            resourceKey,
        };
        const WorldGameplayReplicationPlanner planner;
        WorldResult<std::vector<protocol::v2::EntityStateBatch>> result =
            planner.BuildRemoteEntityStateChunks(100, 5, visibleEntityKeys, gameplayState, entityManager);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<protocol::v2::EntityStateBatch> chunks = result.TakeValue();

        ASSERT_EQ(chunks.size(), 2u);
        ASSERT_EQ(chunks[0].records.size(), 1u);
        ASSERT_EQ(chunks[1].records.size(), 1u);
        EXPECT_EQ(chunks[0].serverTick, 100u);
        EXPECT_EQ(chunks[0].snapshotId, 5u);
        EXPECT_EQ(chunks[0].chunkIndex, 0u);
        EXPECT_EQ(chunks[1].chunkIndex, 1u);
        EXPECT_EQ(chunks[0].chunkCount, 2u);
        EXPECT_EQ(chunks[1].chunkCount, 2u);
        EXPECT_EQ(chunks[0].records[0].entityId, firstPlayerKey.entityId);
        EXPECT_EQ(chunks[1].records[0].entityId, secondPlayerKey.entityId);
        EXPECT_EQ(chunks[0].records[0].bodyTrailSamples.size(), 510u);
        EXPECT_EQ(chunks[1].records[0].bodyTrailSamples.size(), 510u);
        EXPECT_EQ(chunks[0].records[0].boostState, protocol::v2::BoostState::On);
        EXPECT_FLOAT_EQ(chunks[0].records[0].diameter, 1.5f);
        EXPECT_LE(protocol::v2::EntityStateBatch::Wire::CalculatePayloadBytes(chunks[0].records),
                  protocol::v2::EntityStateBatch::Wire::MaximumPayloadBytes);
        EXPECT_LE(protocol::v2::EntityStateBatch::Wire::CalculatePayloadBytes(chunks[1].records),
                  protocol::v2::EntityStateBatch::Wire::MaximumPayloadBytes);
    }

    TEST(WorldGameplayReplicationTests, ProjectsCanonicalAlivePlayerStateToOverviewInput)
    {
        const WorldGameplayConfig config = MakeGameplayConfig();
        const WorldPhysicsArenaBounds mapBounds{-100.0f, -80.0f, 100.0f, 80.0f};
        WorldGameplayState gameplayState = MakeGameplayState(config, mapBounds);

        WorldEntityComponents components;
        components.replicationMetadata.entityKind = WorldEntityKind::Player;
        components.playerControl.playerId = 10;
        ASSERT_TRUE(InitializeBodyTrail(2, &components.bodyTrail));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{4.0f, 5.0f}));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{3.0f, 5.0f}));
        WorldEntityManager entityManager;
        WorldEntityKey entityKey;
        EntityHandle entityHandle;
        ASSERT_TRUE(entityManager.TryCreate(components, &entityKey, &entityHandle));
        ASSERT_EQ(gameplayState.TryRegisterPlayer(10, entityKey), WorldPlayerScoreRegisterResult::Registered);

        const WorldActiveArea activeArea{0.0f, 0.0f, 80.0f, 0.5f, 40.0f};
        const WorldGameplayReplicationPlanner planner;
        WorldResult<WorldOverviewPlanInput> result =
            planner.BuildOverviewInput(120, 7, mapBounds, activeArea, gameplayState, entityManager);
        ASSERT_TRUE(result.Succeeded());
        const WorldOverviewPlanInput input = result.TakeValue();

        EXPECT_EQ(input.serverTick, 120u);
        EXPECT_EQ(input.overviewId, 7u);
        EXPECT_FLOAT_EQ(input.mapMinX, -100.0f);
        EXPECT_FLOAT_EQ(input.mapMaxY, 80.0f);
        EXPECT_FLOAT_EQ(input.activeAreaRadius, 40.0f);
        ASSERT_EQ(input.alivePlayers.size(), 1u);
        EXPECT_EQ(input.alivePlayers[0].playerId, 10u);
        EXPECT_EQ(input.alivePlayers[0].growthPoint, 0u);
        EXPECT_EQ(input.alivePlayers[0].bodySamples,
                  (std::vector<protocol::v2::WorldOverviewPoint>{{4.0f, 5.0f}, {3.0f, 5.0f}}));
    }

    TEST(WorldGameplayReplicationTests, RejectsMissingAliveOverviewEntity)
    {
        const WorldGameplayConfig config = MakeGameplayConfig();
        const WorldPhysicsArenaBounds mapBounds{-100.0f, -80.0f, 100.0f, 80.0f};
        WorldGameplayState gameplayState = MakeGameplayState(config, mapBounds);
        ASSERT_EQ(gameplayState.TryRegisterPlayer(10, WorldEntityKey{77, 1}),
                  WorldPlayerScoreRegisterResult::Registered);
        const WorldEntityManager entityManager;
        const WorldActiveArea activeArea{0.0f, 0.0f, 80.0f, 0.5f, 40.0f};
        const WorldGameplayReplicationPlanner planner;
        const WorldResult<WorldOverviewPlanInput> result =
            planner.BuildOverviewInput(120, 7, mapBounds, activeArea, gameplayState, entityManager);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world::tests
