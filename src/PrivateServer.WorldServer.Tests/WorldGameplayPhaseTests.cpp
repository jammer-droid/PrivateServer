#include "pch.h"

#include "WorldGameplayCommitter.h"
#include "WorldGameplayPhase.h"
#include "WorldResourceSpawnPlanner.h"

#include <utility>
#include <vector>

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

        struct GameplayFixture final
        {
            WorldGameplayConfig config;
            WorldGameplayState state;
            WorldEntityManager entityManager;
            WorldEntityKey firstPlayerKey{};
            WorldEntityKey secondPlayerKey{};
            EntityHandle firstPlayerHandle{};
            EntityHandle secondPlayerHandle{};
        };

        [[nodiscard]] WorldPhysicsArenaBounds MakeArenaBounds() noexcept
        {
            return WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f};
        }

        [[nodiscard]] constexpr WorldActiveArea MakeActiveArea() noexcept
        {
            return WorldActiveArea{0.0f, 0.0f, 100.0f, 1.0f, 100.0f};
        }

        [[nodiscard]] WorldGameplayConfig MakeConfig()
        {
            WorldGameplayConfig config;
            config.minimumPlayersToStart = 2;
            config.scoreToWin = 5;
            config.roundDurationTicks = 1200;
            config.endedDurationTicks = 60;
            config.resourceArchetypeId = 2;
            config.resourceCircleRadius = 0.5f;
            config.resourceScoreValue = 1;
            config.resourceDensityPerUnit2 = 0.000064f;
            config.boostCost = WorldBoostCostConfig{WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f}, 4.0f};
            return config;
        }

        [[nodiscard]] WorldEntityComponents MakePlayerComponents(const std::uint32_t playerId)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{};
            components.motion = MotionComponent{};
            components.movementCapability = MovementCapabilityComponent{10.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 1, WorldShapeKind::Circle, 0.5f};
            components.playerControl = PlayerControlComponent{playerId};
            return components;
        }

        [[nodiscard]] GameplayFixture MakeFixture()
        {
            GameplayFixture fixture;
            fixture.config = MakeConfig();
            WorldResult<WorldGameplayState> gameplayStateResult =
                CreateWorldGameplayState(fixture.config, MakeArenaBounds());
            EXPECT_TRUE(gameplayStateResult.Succeeded());
            fixture.state = gameplayStateResult.TakeValue();
            EXPECT_TRUE(fixture.entityManager.TryCreate(MakePlayerComponents(10), &fixture.firstPlayerKey,
                                                        &fixture.firstPlayerHandle));
            EXPECT_TRUE(fixture.entityManager.TryCreate(MakePlayerComponents(20), &fixture.secondPlayerKey,
                                                        &fixture.secondPlayerHandle));
            EXPECT_EQ(fixture.state.TryRegisterPlayer(10, fixture.firstPlayerKey),
                      WorldPlayerScoreRegisterResult::Registered);
            EXPECT_EQ(fixture.state.TryRegisterPlayer(20, fixture.secondPlayerKey),
                      WorldPlayerScoreRegisterResult::Registered);
            std::vector<WorldResourceSpawnRequest> spawnRequests;
            std::vector<WorldResourcePosition> reservedPositions;
            for (const WorldResourceSlotState& slot : fixture.state.ResourceSlots())
            {
                WorldResult<WorldResourceSpawnPlan> result = WorldResourceSpawnPlanner::PlanAmbient(
                    MakeActiveArea(), fixture.config.resourceCircleRadius, 1, slot.slotId,
                    fixture.state.ResourceRegistry(), reservedPositions);
                EXPECT_TRUE(result.Succeeded());
                const WorldResourceSpawnPlan spawnPlan = result.TakeValue();
                EXPECT_EQ(spawnPlan.requests.size(), 1u);
                const WorldResourceSpawnRequest& request = spawnPlan.requests[0];
                spawnRequests.push_back(request);
                reservedPositions.push_back(WorldResourcePosition{request.positionX, request.positionY});
            }
            const WorldGameplayPhaseResult startResult{
                1,
                {},
                {},
                std::move(spawnRequests),
                WorldGameplayRoundTransition{
                    WorldGameplayRoundTransitionKind::Started,
                    WorldRoundRuntimeState{fixture.state.RoundState().roundId, WorldRoundPhase::Running, 1201, 0},
                },
            };
            WorldGameplayCommitReport report;
            EXPECT_EQ(WorldGameplayCommitter::Commit(fixture.config, startResult, fixture.entityManager, fixture.state,
                                                     &report),
                      WorldGameplayCommitResult::Committed);
            return fixture;
        }

        [[nodiscard]] WorldTriggerOverlap MakeOverlap(const WorldEntityKey firstKey, const std::uint32_t firstFixtureId,
                                                      const WorldEntityKey secondKey,
                                                      const std::uint32_t secondFixtureId) noexcept
        {
            return WorldTriggerOverlap{
                PhysicsProxyKey{firstKey, PhysicsFixtureId{firstFixtureId}},
                PhysicsProxyKey{secondKey, PhysicsFixtureId{secondFixtureId}},
            };
        }

        [[nodiscard]] WorldResourceInstance FindAmbientResource(const GameplayFixture& fixture,
                                                                const std::uint32_t slotId)
        {
            WorldResourceInstance resource;
            EXPECT_TRUE(fixture.state.ResourceRegistry().TryFindAmbientSlot(slotId, &resource));
            return resource;
        }

        [[nodiscard]] WorldResult<void> Compute(
            const GameplayFixture& fixture, const WorldRoundPhase phase,
            const std::vector<WorldTriggerOverlap>& overlaps, WorldGameplayPhaseResult* const outResult,
            const std::span<const WorldPlayerScore> playerScores = {},
            const std::span<const WorldResourceSlotState> resourceSlots = {}, const std::uint32_t serverTick = 50,
            const std::uint32_t phaseEndsAtServerTick = 100, const std::uint32_t winnerPlayerId = 0,
            const std::span<const WorldEntityKey> collisionDeathSet = {},
            const std::span<const WorldEntityKey> activeAreaBoundaryDeathSet = {},
            const WorldActiveArea* const activeArea = nullptr,
            const std::span<const WorldDisconnectDropSnapshot> disconnectDropSnapshots = {})
        {
            const WorldPhysicsStepResult physicsResult{{}, {}, overlaps};
            const WorldReadView readView{fixture.entityManager};
            constexpr WorldActiveArea DefaultActiveArea{0.0f, 0.0f, 100.0f, 1.0f, 100.0f};
            const WorldActiveArea* effectiveActiveArea = activeArea;
            if (effectiveActiveArea == nullptr)
            {
                effectiveActiveArea = &DefaultActiveArea;
            }
            const std::span<const WorldPlayerScore> effectiveScores =
                playerScores.empty() ? fixture.state.PlayerScores() : playerScores;
            const std::span<const WorldResourceSlotState> effectiveSlots =
                resourceSlots.empty() ? fixture.state.ResourceSlots() : resourceSlots;
            WorldResult<WorldGameplayPhaseResult> result = WorldGameplayPhase::Compute(
                serverTick, fixture.config, physicsResult, readView,
                WorldRoundRuntimeState{1, phase, phaseEndsAtServerTick, winnerPlayerId}, effectiveScores,
                effectiveSlots, fixture.state.ResourceRegistry(), collisionDeathSet, activeAreaBoundaryDeathSet,
                disconnectDropSnapshots, 0.05f, effectiveActiveArea);
            if (result.Failed())
            {
                return WorldResult<void>::Failure(result.Error());
            }
            *outResult = result.TakeValue();
            return WorldResult<void>::Success();
        }
    } // namespace

    TEST(WorldGameplayPhaseTests, FindsOnlyAmbientPickupForRequestedSlot)
    {
        const std::vector<WorldResourcePickup> pickups{
            WorldResourcePickup{1},
            WorldResourcePickup{0},
        };

        EXPECT_TRUE(WorldResourcePickup::ContainsAmbientPickupForSlot(pickups, 1));
        EXPECT_FALSE(WorldResourcePickup::ContainsAmbientPickupForSlot(pickups, 2));
        EXPECT_FALSE(WorldResourcePickup::ContainsAmbientPickupForSlot(pickups, 0));
    }

    TEST(WorldGameplayPhaseTests, ChoosesSameStableWinnerAcrossPairDirectionAndInputOrder)
    {
        const GameplayFixture fixture = MakeFixture();
        const WorldEntityKey resourceKey =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[0].slotId).entityKey;
        const std::vector<WorldTriggerOverlap> forwardOverlaps{
            MakeOverlap(resourceKey, 3, fixture.secondPlayerKey, 2),
            MakeOverlap(fixture.firstPlayerKey, 4, resourceKey, 3),
            MakeOverlap(resourceKey, 3, fixture.firstPlayerKey, 4),
        };
        const std::vector<WorldTriggerOverlap> reversedOverlaps{
            MakeOverlap(resourceKey, 3, fixture.firstPlayerKey, 4),
            MakeOverlap(fixture.secondPlayerKey, 2, resourceKey, 3),
        };
        WorldGameplayPhaseResult forwardResult;
        WorldGameplayPhaseResult reversedResult;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, forwardOverlaps, &forwardResult).Succeeded());
        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, reversedOverlaps, &reversedResult).Succeeded());

        ASSERT_EQ(forwardResult.Pickups().size(), 1u);
        ASSERT_EQ(reversedResult.Pickups().size(), 1u);
        EXPECT_EQ(forwardResult.Pickups()[0], reversedResult.Pickups()[0]);
        EXPECT_EQ(forwardResult.Pickups()[0].playerId, 10u);
        EXPECT_EQ(forwardResult.Pickups()[0].playerEntityKey, fixture.firstPlayerKey);
        EXPECT_EQ(forwardResult.Pickups()[0].resourceEntityKey, resourceKey);
        ASSERT_EQ(forwardResult.ScoreAwards().size(), 1u);
        ASSERT_EQ(reversedResult.ScoreAwards().size(), 1u);
        EXPECT_EQ(forwardResult.ScoreAwards()[0], reversedResult.ScoreAwards()[0]);
        EXPECT_EQ(forwardResult.ScoreAwards()[0],
                  (WorldPlayerScoreAward{10, fixture.firstPlayerKey, fixture.config.resourceScoreValue}));
    }

    TEST(WorldGameplayPhaseTests, ClassifiesDeathDropPickupWithoutAmbientSlot)
    {
        GameplayFixture fixture = MakeFixture();
        const WorldResourceSlotState collectedAmbientSlot = fixture.state.ResourceSlots()[0];
        const WorldResourceInstance collectedAmbientResource =
            FindAmbientResource(fixture, collectedAmbientSlot.slotId);
        const WorldGameplayPhaseResult ambientPickupResult{
            1,
            {WorldResourcePickup{collectedAmbientSlot.slotId, collectedAmbientResource.entityKey,
                                 collectedAmbientResource.entityHandle, PhysicsFixtureId{1}, 10, fixture.firstPlayerKey,
                                 fixture.firstPlayerHandle, PhysicsFixtureId{1}, 1}},
            {WorldPlayerScoreAward{10, fixture.firstPlayerKey, 1}},
        };
        WorldGameplayCommitReport ambientPickupReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(fixture.config, ambientPickupResult, fixture.entityManager,
                                                        fixture.state, &ambientPickupReport),
                  WorldGameplayCommitResult::Committed);
        const WorldResourceSpawnRequest deathDropRequest{fixture.firstPlayerKey, 0, 10.0f, 10.0f};
        const WorldGameplayPhaseResult deathResult{
            2,
            {},
            {},
            {},
            WorldGameplayRoundTransition{},
            {},
            {WorldPlayerDeathCommit{10, fixture.firstPlayerKey, WorldPlayerDeathCauseFlags::Collision}},
            {deathDropRequest},
        };
        WorldGameplayCommitReport deathReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(fixture.config, deathResult, fixture.entityManager, fixture.state,
                                                 &deathReport),
                  WorldGameplayCommitResult::Committed);
        ASSERT_EQ(deathReport.spawnedResourceKeys.size(), 1u);
        const WorldEntityKey deathDropKey = deathReport.spawnedResourceKeys[0];
        const std::vector<WorldTriggerOverlap> overlaps{
            MakeOverlap(deathDropKey, 3, fixture.secondPlayerKey, 2),
        };
        WorldGameplayPhaseResult pickupResult;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, overlaps, &pickupResult).Succeeded());

        ASSERT_EQ(pickupResult.Pickups().size(), 1u);
        EXPECT_EQ(pickupResult.Pickups()[0].resourceSlotId, 0u);
        EXPECT_EQ(pickupResult.Pickups()[0].resourceEntityKey, deathDropKey);
        EXPECT_EQ(pickupResult.Pickups()[0].playerId, 20u);
        ASSERT_EQ(pickupResult.ScoreAwards().size(), 1u);
        EXPECT_EQ(pickupResult.ScoreAwards()[0],
                  (WorldPlayerScoreAward{20, fixture.secondPlayerKey, fixture.config.resourceScoreValue}));
    }

    TEST(WorldGameplayPhaseTests, ComputesBoostCostBeforePickupAward)
    {
        GameplayFixture fixture = MakeFixture();
        WorldEntityComponents components;
        ASSERT_TRUE(fixture.entityManager.TryReadComponents(fixture.firstPlayerHandle, &components));
        components.playerControl.boostState = WorldBoostState::On;
        ASSERT_TRUE(fixture.entityManager.TryReplaceComponents(fixture.firstPlayerHandle, components));

        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = 1;
        scores[0].boostCostAccumulator = 0.9f;
        const WorldEntityKey resourceKey =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[0].slotId).entityKey;
        const std::vector<WorldTriggerOverlap> overlaps{
            MakeOverlap(resourceKey, 3, fixture.firstPlayerKey, 2),
        };
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, overlaps, &result, scores).Succeeded());
        ASSERT_EQ(result.BoostCosts().size(), 1u);
        EXPECT_EQ(result.BoostCosts()[0].nextState.growthPoint, 0u);
        EXPECT_NEAR(result.BoostCosts()[0].nextState.boostCostAccumulator, 0.105f, 0.000001f);
        ASSERT_EQ(result.ScoreAwards().size(), 1u);
        EXPECT_EQ(result.ScoreAwards()[0].scoreAward, 1u);
    }

    TEST(WorldGameplayPhaseTests, ExcludesCollisionDeathFromBoostAndPickupInSameTick)
    {
        GameplayFixture fixture = MakeFixture();
        WorldEntityComponents components;
        ASSERT_TRUE(fixture.entityManager.TryReadComponents(fixture.firstPlayerHandle, &components));
        components.playerControl.boostState = WorldBoostState::On;
        ASSERT_TRUE(InitializeBodyTrail(2, &components.bodyTrail));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{1.0f, 0.0f}));
        ASSERT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{2.0f, 0.0f}));
        ASSERT_TRUE(fixture.entityManager.TryReplaceComponents(fixture.firstPlayerHandle, components));

        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = 1;
        const WorldEntityKey resourceKey =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[0].slotId).entityKey;
        const std::vector<WorldTriggerOverlap> overlaps{
            MakeOverlap(resourceKey, 3, fixture.firstPlayerKey, 2),
        };
        const std::vector<WorldEntityKey> collisionDeathSet{fixture.firstPlayerKey};
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(
            Compute(fixture, WorldRoundPhase::Running, overlaps, &result, scores, {}, 50, 100, 0, collisionDeathSet)
                .Succeeded());

        ASSERT_EQ(result.PlayerDeaths().size(), 1u);
        EXPECT_EQ(result.PlayerDeaths()[0],
                  (WorldPlayerDeathCommit{10, fixture.firstPlayerKey, WorldPlayerDeathCauseFlags::Collision}));
        EXPECT_TRUE(result.BoostCosts().empty());
        EXPECT_TRUE(result.Pickups().empty());
        EXPECT_TRUE(result.ScoreAwards().empty());
    }

    TEST(WorldGameplayPhaseTests, MergesPlayerDeathSetsWithoutLosingCauses)
    {
        const GameplayFixture fixture = MakeFixture();
        const std::vector<WorldEntityKey> collisionDeathSet{fixture.firstPlayerKey};
        const std::vector<WorldEntityKey> activeAreaBoundaryDeathSet{
            fixture.firstPlayerKey,
            fixture.secondPlayerKey,
        };
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, {}, &result, {}, {}, 50, 100, 0, collisionDeathSet,
                            activeAreaBoundaryDeathSet)
                        .Succeeded());

        ASSERT_EQ(result.PlayerDeaths().size(), 2u);
        EXPECT_EQ(result.PlayerDeaths()[0],
                  (WorldPlayerDeathCommit{10, fixture.firstPlayerKey,
                                          WorldPlayerDeathCauseFlags::Collision |
                                              WorldPlayerDeathCauseFlags::ActiveAreaBoundary}));
        EXPECT_EQ(result.PlayerDeaths()[1], (WorldPlayerDeathCommit{20, fixture.secondPlayerKey,
                                                                    WorldPlayerDeathCauseFlags::ActiveAreaBoundary}));
    }

    TEST(WorldGameplayPhaseTests, PlansOnlyCollisionDeathDropsAndCountsRejectedPlacements)
    {
        GameplayFixture fixture = MakeFixture();
        WorldEntityComponents firstComponents;
        WorldEntityComponents secondComponents;
        ASSERT_TRUE(fixture.entityManager.TryReadComponents(fixture.firstPlayerHandle, &firstComponents));
        ASSERT_TRUE(fixture.entityManager.TryReadComponents(fixture.secondPlayerHandle, &secondComponents));
        ASSERT_TRUE(InitializeBodyTrail(2, &firstComponents.bodyTrail));
        ASSERT_TRUE(firstComponents.bodyTrail.TryPushBack(BodyTrailSample{1.0f, 0.0f}));
        ASSERT_TRUE(firstComponents.bodyTrail.TryPushBack(BodyTrailSample{2.0f, 0.0f}));
        ASSERT_TRUE(InitializeBodyTrail(2, &secondComponents.bodyTrail));
        ASSERT_TRUE(secondComponents.bodyTrail.TryPushBack(BodyTrailSample{1.0f, 1.0f}));
        ASSERT_TRUE(secondComponents.bodyTrail.TryPushBack(BodyTrailSample{2.0f, 1.0f}));
        ASSERT_TRUE(fixture.entityManager.TryReplaceComponents(fixture.firstPlayerHandle, firstComponents));
        ASSERT_TRUE(fixture.entityManager.TryReplaceComponents(fixture.secondPlayerHandle, secondComponents));

        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = 3;
        scores[1].score = 2;
        const std::vector<WorldEntityKey> collisionDeathSet{fixture.firstPlayerKey, fixture.secondPlayerKey};
        const std::vector<WorldEntityKey> activeAreaBoundaryDeathSet{fixture.secondPlayerKey};
        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 2.0f, 1.0f, 2.0f};
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, {}, &result, scores, {}, 50, 100, 0, collisionDeathSet,
                            activeAreaBoundaryDeathSet, &ActiveArea)
                        .Succeeded());

        ASSERT_EQ(result.DeathDropSpawns().size(), 2u);
        EXPECT_EQ(result.DeathDropSpawns()[0],
                  (WorldResourceSpawnRequest{fixture.firstPlayerKey, 0, 0.33333334f, 0.0f}));
        EXPECT_EQ(result.DeathDropSpawns()[1], (WorldResourceSpawnRequest{fixture.firstPlayerKey, 1, 1.0f, 0.0f}));
        EXPECT_EQ(result.DeathDropPlacementFailureCount(), 1u);
    }

    TEST(WorldGameplayPhaseTests, PlansDisconnectDropFromOwnedSnapshotWithoutLivePlayer)
    {
        const GameplayFixture fixture = MakeFixture();
        WorldResult<BodyTrailComponent> bodyTrailResult = CreateBodyTrailComponent(2);
        ASSERT_TRUE(bodyTrailResult.Succeeded());
        BodyTrailComponent bodyTrail = bodyTrailResult.TakeValue();
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{20.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{21.0f, 0.0f}));
        constexpr WorldEntityKey DisconnectedEntityKey{1000, 1};
        const std::vector<WorldDisconnectDropSnapshot> disconnectDropSnapshots{
            WorldDisconnectDropSnapshot{30, DisconnectedEntityKey, 2, TransformComponent{19.0f, 0.0f, 0.0f},
                                        std::move(bodyTrail)},
        };
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, {}, &result, {}, {}, 50, 100, 0, {}, {}, nullptr,
                            disconnectDropSnapshots)
                        .Succeeded());

        EXPECT_TRUE(result.PlayerDeaths().empty());
        ASSERT_EQ(result.DisconnectDrops().size(), 1u);
        EXPECT_EQ(result.DisconnectDrops()[0], (WorldDisconnectDropCommit{30, DisconnectedEntityKey, 2}));
        ASSERT_EQ(result.DeathDropSpawns().size(), 2u);
        EXPECT_EQ(result.DeathDropSpawns()[0], (WorldResourceSpawnRequest{DisconnectedEntityKey, 0, 19.5f, 0.0f}));
        EXPECT_EQ(result.DeathDropSpawns()[1], (WorldResourceSpawnRequest{DisconnectedEntityKey, 1, 20.5f, 0.0f}));
    }

    TEST(WorldGameplayPhaseTests, AllowsOnePlayerToCollectDifferentResourcesInSameTick)
    {
        const GameplayFixture fixture = MakeFixture();
        const WorldEntityKey firstResourceKey =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[0].slotId).entityKey;
        const WorldEntityKey secondResourceKey =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[1].slotId).entityKey;
        const std::vector<WorldTriggerOverlap> overlaps{
            MakeOverlap(fixture.firstPlayerKey, 1, secondResourceKey, 1),
            MakeOverlap(firstResourceKey, 1, fixture.firstPlayerKey, 1),
            MakeOverlap(fixture.secondPlayerKey, 1, secondResourceKey, 1),
        };
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, overlaps, &result).Succeeded());

        ASSERT_EQ(result.Pickups().size(), 2u);
        EXPECT_EQ(result.Pickups()[0].playerId, 10u);
        EXPECT_EQ(result.Pickups()[1].playerId, 10u);
        ASSERT_EQ(result.ScoreAwards().size(), 1u);
        EXPECT_EQ(result.ScoreAwards()[0],
                  (WorldPlayerScoreAward{10, fixture.firstPlayerKey, fixture.config.resourceScoreValue * 2}));
    }

    TEST(WorldGameplayPhaseTests, IgnoresOverlapOutsideRunningAndNonPlayerResourcePairs)
    {
        const GameplayFixture fixture = MakeFixture();
        const std::vector<WorldTriggerOverlap> overlaps{
            MakeOverlap(fixture.firstPlayerKey, 1, fixture.secondPlayerKey, 1),
        };
        WorldGameplayPhaseResult waitingResult;
        WorldGameplayPhaseResult endedResult;
        WorldGameplayPhaseResult runningResult;

        EXPECT_TRUE(Compute(fixture, WorldRoundPhase::Waiting, overlaps, &waitingResult).Succeeded());
        EXPECT_TRUE(Compute(fixture, WorldRoundPhase::Ended, overlaps, &endedResult).Succeeded());
        EXPECT_TRUE(Compute(fixture, WorldRoundPhase::Running, overlaps, &runningResult).Succeeded());
        EXPECT_TRUE(waitingResult.Pickups().empty());
        EXPECT_TRUE(endedResult.Pickups().empty());
        EXPECT_TRUE(runningResult.Pickups().empty());
    }

    TEST(WorldGameplayPhaseTests, RejectsStaleGenerationAndBrokenGameplayMappings)
    {
        const GameplayFixture fixture = MakeFixture();
        const WorldResourceSlotState activeSlot = fixture.state.ResourceSlots()[0];
        const WorldEntityKey activeResourceKey = FindAmbientResource(fixture, activeSlot.slotId).entityKey;
        const WorldEntityKey staleResourceKey{activeResourceKey.entityId, activeResourceKey.generation + 1};
        WorldGameplayPhaseResult result;

        EXPECT_EQ(Compute(fixture, WorldRoundPhase::Running,
                          {MakeOverlap(fixture.firstPlayerKey, 1, staleResourceKey, 1)}, &result)
                      .Error(),
                  WorldErrorCode::InvalidState);

        std::vector<WorldResourceSlotState> brokenSlots(fixture.state.ResourceSlots().begin(),
                                                        fixture.state.ResourceSlots().end());
        brokenSlots[0].phase = WorldResourceSlotPhase::Respawning;
        EXPECT_EQ(Compute(fixture, WorldRoundPhase::Running,
                          {MakeOverlap(fixture.firstPlayerKey, 1, activeResourceKey, 1)}, &result, {}, brokenSlots)
                      .Error(),
                  WorldErrorCode::InvalidState);

        std::vector<WorldPlayerScore> brokenScores(fixture.state.PlayerScores().begin(),
                                                   fixture.state.PlayerScores().end());
        brokenScores[0].controlledEntityKey.generation += 1;
        EXPECT_EQ(Compute(fixture, WorldRoundPhase::Running,
                          {MakeOverlap(fixture.firstPlayerKey, 1, activeResourceKey, 1)}, &result, brokenScores)
                      .Error(),
                  WorldErrorCode::InvalidState);
    }

    TEST(WorldGameplayPhaseTests, PickupTargetDoesNotEndRoundBeforeDeadline)
    {
        const GameplayFixture fixture = MakeFixture();
        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = fixture.config.scoreToWin - 1;
        scores[1].score = fixture.config.scoreToWin - 2;
        const WorldEntityKey resourceKey =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[0].slotId).entityKey;
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, {MakeOverlap(fixture.firstPlayerKey, 1, resourceKey, 1)},
                            &result, scores, {}, 49, 50)
                        .Succeeded());

        EXPECT_EQ(result.RoundTransition().kind, WorldGameplayRoundTransitionKind::None);
        EXPECT_EQ(result.ResourceSpawns().size(), 1u);
    }

    TEST(WorldGameplayPhaseTests, PickupPlansSameTickAmbientRefillFromProjectedCount)
    {
        const GameplayFixture fixture = MakeFixture();
        const WorldResourceSlotState collectedSlot = fixture.state.ResourceSlots()[0];
        const WorldResourceInstance collectedResource = FindAmbientResource(fixture, collectedSlot.slotId);
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running,
                            {MakeOverlap(fixture.firstPlayerKey, 1, collectedResource.entityKey, 1)}, &result)
                        .Succeeded());

        ASSERT_EQ(result.Pickups().size(), 1u);
        ASSERT_EQ(result.ResourceSpawns().size(), 1u);
        EXPECT_EQ(result.ResourceSpawns()[0].ambientSlotId, collectedSlot.slotId);
        EXPECT_TRUE(result.ResourceSpawns()[0].positionX != collectedResource.positionX ||
                    result.ResourceSpawns()[0].positionY != collectedResource.positionY);
    }

    TEST(WorldGameplayPhaseTests, PlansCleanupForEveryResourceOutsideCurrentActiveArea)
    {
        const GameplayFixture fixture = MakeFixture();
        constexpr WorldActiveArea DistantActiveArea{1000.0f, 1000.0f, 1.0f, 1.0f, 1.0f};
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(
            Compute(fixture, WorldRoundPhase::Running, {}, &result, {}, {}, 50, 100, 0, {}, {}, &DistantActiveArea)
                .Succeeded());

        EXPECT_EQ(std::vector<WorldResourceInstance>(result.OutsideResourceRemovals().begin(),
                                                     result.OutsideResourceRemovals().end()),
                  std::vector<WorldResourceInstance>(fixture.state.ResourceRegistry().Instances().begin(),
                                                     fixture.state.ResourceRegistry().Instances().end()));
        EXPECT_TRUE(result.ResourceSpawns().empty());
    }

    TEST(WorldGameplayPhaseTests, DeathDropAtProjectedTargetSuppressesAmbientRefill)
    {
        GameplayFixture fixture = MakeFixture();
        const WorldResourceInstance collectedResource =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[0].slotId);
        WorldEntityComponents firstPlayerComponents;
        ASSERT_TRUE(fixture.entityManager.TryReadComponents(fixture.firstPlayerHandle, &firstPlayerComponents));
        ASSERT_TRUE(InitializeBodyTrail(1, &firstPlayerComponents.bodyTrail));
        ASSERT_TRUE(firstPlayerComponents.bodyTrail.TryPushBack(BodyTrailSample{1.0f, 0.0f}));
        ASSERT_TRUE(fixture.entityManager.TryReplaceComponents(fixture.firstPlayerHandle, firstPlayerComponents));
        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = 1;
        const std::vector<WorldEntityKey> collisionDeathSet{fixture.firstPlayerKey};
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running,
                            {MakeOverlap(fixture.secondPlayerKey, 1, collectedResource.entityKey, 1)}, &result, scores,
                            {}, 50, 100, 0, collisionDeathSet)
                        .Succeeded());

        ASSERT_EQ(result.Pickups().size(), 1u);
        ASSERT_EQ(result.DeathDropSpawns().size(), 1u);
        EXPECT_TRUE(result.ResourceSpawns().empty());
    }

    TEST(WorldGameplayPhaseTests, ProjectedShortageRefillAvoidsDeathDropPlannedEarlierInSameTick)
    {
        GameplayFixture fixture = MakeFixture();
        const WorldResourceSlotState firstSlot = fixture.state.ResourceSlots()[0];
        const WorldResourceInstance firstResource = FindAmbientResource(fixture, firstSlot.slotId);
        const WorldResourceInstance secondResource =
            FindAmbientResource(fixture, fixture.state.ResourceSlots()[1].slotId);
        WorldResult<WorldResourceSpawnPlan> firstAmbientPlanResult =
            WorldResourceSpawnPlanner::PlanAmbient(MakeActiveArea(), fixture.config.resourceCircleRadius, 50,
                                                   firstSlot.slotId, fixture.state.ResourceRegistry(), {});
        ASSERT_TRUE(firstAmbientPlanResult.Succeeded());
        const WorldResourceSpawnPlan firstAmbientPlan = firstAmbientPlanResult.TakeValue();
        ASSERT_EQ(firstAmbientPlan.requests.size(), 1u);
        const WorldResourceSpawnRequest& firstAmbientRequest = firstAmbientPlan.requests[0];

        WorldEntityComponents firstPlayerComponents;
        ASSERT_TRUE(fixture.entityManager.TryReadComponents(fixture.firstPlayerHandle, &firstPlayerComponents));
        ASSERT_TRUE(InitializeBodyTrail(1, &firstPlayerComponents.bodyTrail));
        ASSERT_TRUE(firstPlayerComponents.bodyTrail.TryPushBack(
            BodyTrailSample{firstAmbientRequest.positionX * 2.0f, firstAmbientRequest.positionY * 2.0f}));
        ASSERT_TRUE(fixture.entityManager.TryReplaceComponents(fixture.firstPlayerHandle, firstPlayerComponents));

        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = 1;
        const std::vector<WorldEntityKey> collisionDeathSet{fixture.firstPlayerKey};
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running,
                            {
                                MakeOverlap(fixture.secondPlayerKey, 1, firstResource.entityKey, 1),
                                MakeOverlap(fixture.secondPlayerKey, 1, secondResource.entityKey, 1),
                            },
                            &result, scores, {}, 50, 100, 0, collisionDeathSet)
                        .Succeeded());

        ASSERT_EQ(result.Pickups().size(), 2u);
        ASSERT_EQ(result.DeathDropSpawns().size(), 1u);
        EXPECT_FLOAT_EQ(result.DeathDropSpawns()[0].positionX, firstAmbientRequest.positionX);
        EXPECT_FLOAT_EQ(result.DeathDropSpawns()[0].positionY, firstAmbientRequest.positionY);
        ASSERT_EQ(result.ResourceSpawns().size(), 1u);
        EXPECT_EQ(result.ResourceSpawns()[0].sourceOrdinal, 1u);
        EXPECT_TRUE(result.ResourceSpawns()[0].positionX != result.DeathDropSpawns()[0].positionX ||
                    result.ResourceSpawns()[0].positionY != result.DeathDropSpawns()[0].positionY);
    }

    TEST(WorldGameplayPhaseTests, DeadlineEndsWithoutSelectingLegacyWinner)
    {
        const GameplayFixture fixture = MakeFixture();
        std::vector<WorldPlayerScore> scores(fixture.state.PlayerScores().begin(), fixture.state.PlayerScores().end());
        scores[0].score = 2;
        scores[1].score = 2;
        WorldGameplayPhaseResult result;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Running, {}, &result, scores, {}, 50, 50).Succeeded());

        EXPECT_EQ(result.RoundTransition().kind, WorldGameplayRoundTransitionKind::Ended);
        EXPECT_EQ(result.RoundTransition().nextState, (WorldRoundRuntimeState{1, WorldRoundPhase::Ended, 50, 0}));
    }

    TEST(WorldGameplayPhaseTests, EndedHoldsWithoutAutomaticRematch)
    {
        const GameplayFixture fixture = MakeFixture();
        constexpr WorldActiveArea ActiveArea{10.0f, -5.0f, 100.0f, 0.5f, 50.0f};
        WorldGameplayPhaseResult held;
        WorldGameplayPhaseResult afterFormerDeadline;

        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Ended, {}, &held, {}, {}, 109, 110, 10).Succeeded());
        ASSERT_TRUE(Compute(fixture, WorldRoundPhase::Ended, {}, &afterFormerDeadline, {}, {}, 200, 110, 10, {}, {},
                            &ActiveArea)
                        .Succeeded());

        EXPECT_EQ(held.RoundTransition().kind, WorldGameplayRoundTransitionKind::None);
        EXPECT_TRUE(held.ResourceSpawns().empty());
        EXPECT_EQ(afterFormerDeadline.RoundTransition().kind, WorldGameplayRoundTransitionKind::None);
        EXPECT_TRUE(afterFormerDeadline.ResourceSpawns().empty());
    }
} // namespace psnr::world::tests
