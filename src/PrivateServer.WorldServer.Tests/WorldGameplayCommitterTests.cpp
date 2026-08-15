#include "pch.h"

#include "WorldGameplayCommitter.h"
#include "WorldReadView.h"
#include "WorldResourceSpawnPlanner.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        struct PlayerEntity final
        {
            std::uint32_t playerId = 0;
            WorldEntityKey key{};
            EntityHandle handle{};
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

        [[nodiscard]] WorldGameplayState MakeState(const WorldGameplayConfig& config)
        {
            WorldResult<WorldGameplayState> result = CreateWorldGameplayState(config, MakeArenaBounds());
            EXPECT_TRUE(result.Succeeded());
            return result.TakeValue();
        }

        [[nodiscard]] WorldEntityComponents MakePlayerComponents(const std::uint32_t playerId)
        {
            WorldEntityComponents components;
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 1, WorldShapeKind::Circle, 0.5f};
            components.movementCapability = MovementCapabilityComponent{10.0f};
            components.playerControl = PlayerControlComponent{playerId};
            return components;
        }

        [[nodiscard]] PlayerEntity CreatePlayer(const std::uint32_t playerId, WorldEntityManager* const entityManager,
                                                WorldGameplayState* const gameplayState)
        {
            PlayerEntity player;
            player.playerId = playerId;
            EXPECT_TRUE(entityManager->TryCreate(MakePlayerComponents(playerId), &player.key, &player.handle));
            EXPECT_EQ(gameplayState->TryRegisterPlayer(playerId, player.key),
                      WorldPlayerScoreRegisterResult::Registered);
            return player;
        }

        [[nodiscard]] WorldResourceInstance FindAmbientResource(const WorldGameplayState& state,
                                                                const std::uint32_t slotId)
        {
            WorldResourceInstance resource;
            EXPECT_TRUE(state.ResourceRegistry().TryFindAmbientSlot(slotId, &resource));
            return resource;
        }

        [[nodiscard]] WorldResourcePickup MakePickup(const WorldGameplayState& state,
                                                     const WorldResourceSlotState& slot, const PlayerEntity& player,
                                                     const std::uint32_t scoreAward)
        {
            const WorldResourceInstance resource = FindAmbientResource(state, slot.slotId);
            return WorldResourcePickup{
                slot.slotId, resource.entityKey, resource.entityHandle, PhysicsFixtureId{1}, player.playerId,
                player.key,  player.handle,      PhysicsFixtureId{1},   scoreAward,
            };
        }

        [[nodiscard]] WorldGameplayPhaseResult MakePhaseResult(const std::uint32_t serverTick,
                                                               std::vector<WorldResourcePickup> pickups,
                                                               std::vector<WorldPlayerScoreAward> scoreAwards)
        {
            return WorldGameplayPhaseResult{serverTick, std::move(pickups), std::move(scoreAwards)};
        }

        [[nodiscard]] WorldResourceSpawnRequest MakeSpawnRequest(
            const WorldGameplayConfig& config, const std::uint32_t serverTick, const std::uint32_t slotId,
            const WorldResourceRegistry& resourceRegistry,
            const std::span<const WorldResourcePosition> reservedPositions)
        {
            WorldResult<WorldResourceSpawnPlan> result = WorldResourceSpawnPlanner::PlanAmbient(
                MakeActiveArea(), config.resourceCircleRadius, serverTick, slotId, resourceRegistry, reservedPositions);
            EXPECT_TRUE(result.Succeeded());
            const WorldResourceSpawnPlan plan = result.TakeValue();
            EXPECT_EQ(plan.requests.size(), 1u);
            return plan.requests[0];
        }

        [[nodiscard]] std::vector<WorldResourceSpawnRequest> MakeAllSpawnRequests(const WorldGameplayConfig& config,
                                                                                  const WorldGameplayState& state,
                                                                                  const std::uint32_t serverTick)
        {
            std::vector<WorldResourceSpawnRequest> requests;
            std::vector<WorldResourcePosition> reservedPositions;
            for (const WorldResourceSlotState& slot : state.ResourceSlots())
            {
                const WorldResourceSpawnRequest request =
                    MakeSpawnRequest(config, serverTick, slot.slotId, state.ResourceRegistry(), reservedPositions);
                requests.push_back(request);
                reservedPositions.push_back(WorldResourcePosition{request.positionX, request.positionY});
            }
            return requests;
        }

        [[nodiscard]] WorldGameplayCommitResult StartRoundWithResources(const WorldGameplayConfig& config,
                                                                        const std::uint32_t serverTick,
                                                                        WorldEntityManager& entityManager,
                                                                        WorldGameplayState& state)
        {
            const WorldGameplayPhaseResult phaseResult{
                serverTick,
                {},
                {},
                MakeAllSpawnRequests(config, state, serverTick),
                WorldGameplayRoundTransition{
                    WorldGameplayRoundTransitionKind::Started,
                    WorldRoundRuntimeState{state.RoundState().roundId, WorldRoundPhase::Running,
                                           serverTick + config.roundDurationTicks, 0},
                },
            };
            WorldGameplayCommitReport report;
            return WorldGameplayCommitter::Commit(config, phaseResult, entityManager, state, &report);
        }
    } // namespace

    TEST(WorldGameplayCommitterTests, SpawnsEveryDormantSlotAsResourceEntity)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;

        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);

        ASSERT_EQ(state.ResourceSlots().size(), 2u);
        EXPECT_EQ(entityManager.Size(), 2u);
        EXPECT_EQ(state.ResourceRegistry().Count(), 2u);
        for (const WorldResourceSlotState& slot : state.ResourceSlots())
        {
            WorldEntityComponents components;
            const WorldResourceInstance resourceInstance = FindAmbientResource(state, slot.slotId);
            ASSERT_EQ(slot.phase, WorldResourceSlotPhase::Active);
            ASSERT_TRUE(entityManager.TryReadComponents(resourceInstance.entityHandle, &components));
            EXPECT_EQ(components.replicationMetadata.entityKind, WorldEntityKind::Resource);
            EXPECT_EQ(components.replicationMetadata.archetypeId, config.resourceArchetypeId);
            EXPECT_EQ(components.replicationMetadata.primaryShapeKind, WorldShapeKind::Circle);
            EXPECT_EQ(components.replicationMetadata.primaryCircleRadius, config.resourceCircleRadius);
            EXPECT_EQ(components.movementCapability.maxMoveSpeed, 0.0f);
            EXPECT_EQ(components.playerControl.playerId, 0u);
            EXPECT_TRUE(components.physicsBinding.proxyIds.empty());

            EXPECT_TRUE(MakeActiveArea().ContainsCircleStrictly(resourceInstance.positionX, resourceInstance.positionY,
                                                                config.resourceCircleRadius));
            EXPECT_EQ(components.transform.positionX, resourceInstance.positionX);
            EXPECT_EQ(components.transform.positionY, resourceInstance.positionY);
            EXPECT_EQ(resourceInstance.origin, WorldResourceOrigin::Ambient);
            EXPECT_EQ(resourceInstance.ambientSlotId, slot.slotId);
            EXPECT_TRUE(
                state.ResourceRegistry().ContainsExactPosition(resourceInstance.positionX, resourceInstance.positionY));
        }
    }

    TEST(WorldGameplayCommitterTests, RejectsMixedSlotStateWithoutCreatingEntities)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;

        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);
        const std::size_t entityCount = entityManager.Size();

        EXPECT_EQ(StartRoundWithResources(config, 1, entityManager, state),
                  WorldGameplayCommitResult::StateInvariantViolation);
        EXPECT_EQ(entityManager.Size(), entityCount);
    }

    TEST(WorldGameplayCommitterTests, CommitsResourceLifetimeAndStableFullScoreSnapshots)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity player20 = CreatePlayer(20, &entityManager, &state);
        const PlayerEntity player10 = CreatePlayer(10, &entityManager, &state);
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);
        const WorldResourceSlotState firstResource = state.ResourceSlots()[0];
        const WorldResourceSlotState secondResource = state.ResourceSlots()[1];
        const WorldResourceInstance firstInstance = FindAmbientResource(state, firstResource.slotId);
        const WorldResourceInstance secondInstance = FindAmbientResource(state, secondResource.slotId);
        const WorldGameplayPhaseResult phaseResult = MakePhaseResult(50,
                                                                     {
                                                                         MakePickup(state, firstResource, player20, 1),
                                                                         MakePickup(state, secondResource, player10, 1),
                                                                     },
                                                                     {
                                                                         WorldPlayerScoreAward{10, player10.key, 1},
                                                                         WorldPlayerScoreAward{20, player20.key, 1},
                                                                     });
        WorldGameplayCommitReport report;

        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, phaseResult, entityManager, state, &report),
                  WorldGameplayCommitResult::Committed);

        EXPECT_EQ(entityManager.Size(), 2u);
        EXPECT_EQ(state.ResourceRegistry().Count(), 0u);
        EntityHandle removedHandle;
        EXPECT_FALSE(entityManager.TryFindHandle(firstInstance.entityKey, &removedHandle));
        EXPECT_FALSE(entityManager.TryFindHandle(secondInstance.entityKey, &removedHandle));
        for (const WorldResourceSlotState& slot : state.ResourceSlots())
        {
            EXPECT_EQ(slot.phase, WorldResourceSlotPhase::Respawning);
        }

        WorldPlayerScore player10Score;
        WorldPlayerScore player20Score;
        ASSERT_TRUE(state.TryFindPlayerScore(10, &player10Score));
        ASSERT_TRUE(state.TryFindPlayerScore(20, &player20Score));
        EXPECT_EQ(player10Score.score, 1u);
        EXPECT_EQ(player20Score.score, 1u);

        EXPECT_EQ(report.serverTick, 50u);
        EXPECT_EQ(report.pickupCount, 2u);
        ASSERT_EQ(report.entityRemovals.size(), 2u);
        EXPECT_EQ(report.entityRemovals[0],
                  (WorldGameplayEntityRemoval{firstInstance.entityKey, WorldGameplayEntityRemoveReason::Collected}));
        EXPECT_EQ(report.entityRemovals[1],
                  (WorldGameplayEntityRemoval{secondInstance.entityKey, WorldGameplayEntityRemoveReason::Collected}));
        ASSERT_EQ(report.scoreSnapshots.size(), 2u);
        EXPECT_EQ(report.scoreSnapshots[0], (WorldGameplayScoreSnapshot{10, player10.key, 1}));
        EXPECT_EQ(report.scoreSnapshots[1], (WorldGameplayScoreSnapshot{20, player20.key, 1}));
    }

    TEST(WorldGameplayCommitterTests, ScoreOverflowDoesNotMutateRemainingResource)
    {
        WorldGameplayConfig config = MakeConfig();
        config.resourceScoreValue = std::numeric_limits<std::uint32_t>::max();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity player = CreatePlayer(10, &entityManager, &state);
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);
        const WorldResourceSlotState firstResource = state.ResourceSlots()[0];
        const WorldResourceSlotState secondResource = state.ResourceSlots()[1];
        const WorldResourceInstance secondInstance = FindAmbientResource(state, secondResource.slotId);
        const WorldGameplayPhaseResult firstResult =
            MakePhaseResult(50, {MakePickup(state, firstResource, player, config.resourceScoreValue)},
                            {WorldPlayerScoreAward{player.playerId, player.key, config.resourceScoreValue}});
        WorldGameplayCommitReport firstReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, firstResult, entityManager, state, &firstReport),
                  WorldGameplayCommitResult::Committed);

        const WorldGameplayPhaseResult overflowResult =
            MakePhaseResult(51, {MakePickup(state, secondResource, player, config.resourceScoreValue)},
                            {WorldPlayerScoreAward{player.playerId, player.key, config.resourceScoreValue}});
        WorldGameplayCommitReport unchangedReport;
        unchangedReport.serverTick = 777;

        EXPECT_EQ(WorldGameplayCommitter::CommitPickups(config, overflowResult, entityManager, state, &unchangedReport),
                  WorldGameplayCommitResult::ArithmeticOverflow);

        WorldPlayerScore score;
        EntityHandle currentHandle;
        ASSERT_TRUE(state.TryFindPlayerScore(player.playerId, &score));
        EXPECT_EQ(score.score, std::numeric_limits<std::uint32_t>::max());
        EXPECT_EQ(state.ResourceSlots()[1], secondResource);
        EXPECT_TRUE(entityManager.TryFindHandle(secondInstance.entityKey, &currentHandle));
        EXPECT_EQ(currentHandle, secondInstance.entityHandle);
        EXPECT_EQ(unchangedReport.serverTick, 777u);
    }

    TEST(WorldGameplayCommitterTests, CommitsBoostCostBeforePickupAward)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity player = CreatePlayer(10, &entityManager, &state);
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);
        const WorldResourceSlotState firstResource = state.ResourceSlots()[0];
        const WorldResourceSlotState secondResource = state.ResourceSlots()[1];
        const WorldGameplayPhaseResult growthResult = MakePhaseResult(50, {MakePickup(state, firstResource, player, 1)},
                                                                      {WorldPlayerScoreAward{10, player.key, 1}});
        WorldGameplayCommitReport growthReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, growthResult, entityManager, state, &growthReport),
                  WorldGameplayCommitResult::Committed);

        const WorldGameplayPhaseResult boostAndPickupResult{
            51,
            {MakePickup(state, secondResource, player, 1)},
            {WorldPlayerScoreAward{10, player.key, 1}},
            {},
            WorldGameplayRoundTransition{},
            {WorldPlayerBoostCostCommit{10, player.key, WorldBoostCostState{0, 0.105f}}},
        };
        WorldGameplayCommitReport report;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, boostAndPickupResult, entityManager, state, &report),
                  WorldGameplayCommitResult::Committed);

        WorldPlayerScore score;
        ASSERT_TRUE(state.TryFindPlayerScore(10, &score));
        EXPECT_EQ(score.score, 1u);
        EXPECT_NEAR(score.boostCostAccumulator, 0.105f, 0.000001f);
        ASSERT_EQ(report.scoreSnapshots.size(), 1u);
        EXPECT_EQ(report.scoreSnapshots[0], (WorldGameplayScoreSnapshot{10, player.key, 1}));
    }

    TEST(WorldGameplayCommitterTests, CommitsCollisionDeathsSimultaneouslyAsSpawnPending)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity firstPlayer = CreatePlayer(10, &entityManager, &state);
        const PlayerEntity secondPlayer = CreatePlayer(20, &entityManager, &state);
        const WorldGameplayPhaseResult phaseResult{
            50,
            {},
            {},
            {},
            WorldGameplayRoundTransition{},
            {},
            {
                WorldPlayerDeathCommit{10, firstPlayer.key, WorldPlayerDeathCauseFlags::Collision},
                WorldPlayerDeathCommit{20, secondPlayer.key, WorldPlayerDeathCauseFlags::Collision},
            },
        };
        WorldGameplayCommitReport report;

        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, phaseResult, entityManager, state, &report),
                  WorldGameplayCommitResult::Committed);

        WorldPlayerScore firstScore;
        WorldPlayerScore secondScore;
        EntityHandle entityHandle;
        ASSERT_TRUE(state.TryFindPlayerScore(10, &firstScore));
        ASSERT_TRUE(state.TryFindPlayerScore(20, &secondScore));
        EXPECT_EQ(firstScore.lifecycle, WorldPlayerLifecycle::SpawnPending);
        EXPECT_EQ(secondScore.lifecycle, WorldPlayerLifecycle::SpawnPending);
        EXPECT_EQ(firstScore.score, 0u);
        EXPECT_EQ(secondScore.score, 0u);
        EXPECT_FALSE(entityManager.TryFindHandle(firstPlayer.key, &entityHandle));
        EXPECT_FALSE(entityManager.TryFindHandle(secondPlayer.key, &entityHandle));
        ASSERT_EQ(report.entityRemovals.size(), 2u);
        EXPECT_EQ(report.entityRemovals[0],
                  (WorldGameplayEntityRemoval{firstPlayer.key, WorldGameplayEntityRemoveReason::PlayerDeath}));
        EXPECT_EQ(report.entityRemovals[1],
                  (WorldGameplayEntityRemoval{secondPlayer.key, WorldGameplayEntityRemoveReason::PlayerDeath}));
        ASSERT_EQ(report.scoreSnapshots.size(), 2u);
        EXPECT_EQ(report.scoreSnapshots[0], (WorldGameplayScoreSnapshot{10, firstPlayer.key, 0}));
        EXPECT_EQ(report.scoreSnapshots[1], (WorldGameplayScoreSnapshot{20, secondPlayer.key, 0}));
    }

    TEST(WorldGameplayCommitterTests, CommitsDeathDropWithNewIdentityAndPickupDoesNotRespawnAmbientSlot)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity deadPlayer = CreatePlayer(10, &entityManager, &state);
        const PlayerEntity collector = CreatePlayer(20, &entityManager, &state);
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);

        const WorldResourceSlotState collectedAmbientSlot = state.ResourceSlots()[0];
        const WorldGameplayPhaseResult ambientPickupResult =
            MakePhaseResult(50, {MakePickup(state, collectedAmbientSlot, deadPlayer, 1)},
                            {WorldPlayerScoreAward{deadPlayer.playerId, deadPlayer.key, 1}});
        WorldGameplayCommitReport ambientPickupReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, ambientPickupResult, entityManager, state,
                                                        &ambientPickupReport),
                  WorldGameplayCommitResult::Committed);

        const WorldResourceSpawnRequest deathDropRequest{deadPlayer.key, 0, 10.0f, 10.0f};
        const WorldGameplayPhaseResult deathResult{
            51,
            {},
            {},
            {},
            WorldGameplayRoundTransition{},
            {},
            {WorldPlayerDeathCommit{deadPlayer.playerId, deadPlayer.key, WorldPlayerDeathCauseFlags::Collision}},
            {deathDropRequest},
            2,
        };
        WorldGameplayCommitReport deathReport;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, deathResult, entityManager, state, &deathReport),
                  WorldGameplayCommitResult::Committed);

        ASSERT_EQ(deathReport.spawnedResourceKeys.size(), 1u);
        const WorldEntityKey deathDropKey = deathReport.spawnedResourceKeys[0];
        EXPECT_NE(deathDropKey, deadPlayer.key);
        WorldResourceInstance deathDrop;
        ASSERT_TRUE(state.ResourceRegistry().TryFind(deathDropKey, &deathDrop));
        EXPECT_EQ(deathDrop.origin, WorldResourceOrigin::DeathDrop);
        EXPECT_EQ(deathDrop.ambientSlotId, 0u);
        EXPECT_EQ(deathDrop.positionX, deathDropRequest.positionX);
        EXPECT_EQ(deathDrop.positionY, deathDropRequest.positionY);
        EXPECT_EQ(state.Metrics().deathDropPlacementFailureCount, 2u);

        const std::vector<WorldResourceSlotState> slotsBeforePickup(state.ResourceSlots().begin(),
                                                                    state.ResourceSlots().end());
        const WorldGameplayPhaseResult deathDropPickupResult = MakePhaseResult(
            52,
            {WorldResourcePickup{0, deathDrop.entityKey, deathDrop.entityHandle, PhysicsFixtureId{1},
                                 collector.playerId, collector.key, collector.handle, PhysicsFixtureId{1}, 1}},
            {WorldPlayerScoreAward{collector.playerId, collector.key, 1}});
        WorldGameplayCommitReport pickupReport;

        ASSERT_EQ(
            WorldGameplayCommitter::CommitPickups(config, deathDropPickupResult, entityManager, state, &pickupReport),
            WorldGameplayCommitResult::Committed);
        EXPECT_EQ(state.Metrics().deathDropPlacementFailureCount, 2u);

        WorldResourceInstance removedDeathDrop;
        EntityHandle removedDeathDropHandle;
        EXPECT_FALSE(state.ResourceRegistry().TryFind(deathDrop.entityKey, &removedDeathDrop));
        EXPECT_FALSE(entityManager.TryFindHandle(deathDrop.entityKey, &removedDeathDropHandle));
        EXPECT_EQ(std::vector<WorldResourceSlotState>(state.ResourceSlots().begin(), state.ResourceSlots().end()),
                  slotsBeforePickup);
        WorldPlayerScore collectorScore;
        ASSERT_TRUE(state.TryFindPlayerScore(collector.playerId, &collectorScore));
        EXPECT_EQ(collectorScore.score, 1u);
    }

    TEST(WorldGameplayCommitterTests, CommitsDisconnectDropWithoutLiveSourcePlayer)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        static_cast<void>(CreatePlayer(10, &entityManager, &state));
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);
        constexpr WorldEntityKey DisconnectedEntityKey{1000, 1};
        const WorldResourceSpawnRequest disconnectDropRequest{DisconnectedEntityKey, 0, 20.0f, 20.0f};
        const WorldGameplayPhaseResult disconnectResult{
            50,
            {},
            {},
            {},
            WorldGameplayRoundTransition{},
            {},
            {},
            {disconnectDropRequest},
            0,
            {},
            {WorldDisconnectDropCommit{30, DisconnectedEntityKey, 1}},
        };
        WorldGameplayCommitReport report;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, disconnectResult, entityManager, state, &report),
                  WorldGameplayCommitResult::Committed);

        ASSERT_EQ(report.spawnedResourceKeys.size(), 1u);
        WorldResourceInstance committedDrop;
        ASSERT_TRUE(state.ResourceRegistry().TryFind(report.spawnedResourceKeys[0], &committedDrop));
        EXPECT_EQ(committedDrop.origin, WorldResourceOrigin::DeathDrop);
        EXPECT_EQ(committedDrop.positionX, 20.0f);
        EXPECT_EQ(committedDrop.positionY, 20.0f);
    }

    TEST(WorldGameplayCommitterTests, MixedAmbientAndDeathDropRegistryConflictRollsBackEverySpawn)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity deadPlayer = CreatePlayer(10, &entityManager, &state);
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);

        const WorldResourceSlotState collectedAmbientSlot = state.ResourceSlots()[0];
        const WorldGameplayPhaseResult pickupResult =
            MakePhaseResult(50, {MakePickup(state, collectedAmbientSlot, deadPlayer, 1)},
                            {WorldPlayerScoreAward{deadPlayer.playerId, deadPlayer.key, 1}});
        WorldGameplayCommitReport pickupReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, pickupResult, entityManager, state, &pickupReport),
                  WorldGameplayCommitResult::Committed);

        const WorldResourceSlotState respawningSlot = state.ResourceSlots()[0];
        ASSERT_EQ(respawningSlot.phase, WorldResourceSlotPhase::Respawning);
        const WorldResourceSpawnRequest ambientRequest =
            MakeSpawnRequest(config, 50, respawningSlot.slotId, state.ResourceRegistry(), {});
        const WorldResourceSpawnRequest conflictingDeathDrop{
            deadPlayer.key,
            0,
            ambientRequest.positionX,
            ambientRequest.positionY,
        };
        const std::size_t entityCountBeforeCommit = entityManager.Size();
        const std::size_t resourceCountBeforeCommit = state.ResourceRegistry().Count();
        const WorldGameplayPhaseResult mixedResult{
            50,
            {},
            {},
            {ambientRequest},
            WorldGameplayRoundTransition{},
            {},
            {WorldPlayerDeathCommit{deadPlayer.playerId, deadPlayer.key, WorldPlayerDeathCauseFlags::Collision}},
            {conflictingDeathDrop},
        };
        WorldGameplayCommitReport unchangedReport;
        unchangedReport.serverTick = 777;

        EXPECT_EQ(WorldGameplayCommitter::Commit(config, mixedResult, entityManager, state, &unchangedReport),
                  WorldGameplayCommitResult::StateInvariantViolation);

        EXPECT_EQ(entityManager.Size(), entityCountBeforeCommit);
        EXPECT_EQ(state.ResourceRegistry().Count(), resourceCountBeforeCommit);
        EXPECT_EQ(state.ResourceSlots()[0], respawningSlot);
        WorldPlayerScore deadPlayerScore;
        EntityHandle deadPlayerHandle;
        ASSERT_TRUE(state.TryFindPlayerScore(deadPlayer.playerId, &deadPlayerScore));
        EXPECT_EQ(deadPlayerScore.lifecycle, WorldPlayerLifecycle::Alive);
        EXPECT_TRUE(entityManager.TryFindHandle(deadPlayer.key, &deadPlayerHandle));
        EXPECT_EQ(deadPlayerHandle, deadPlayer.handle);
        EXPECT_EQ(unchangedReport.serverTick, 777u);
    }

    TEST(WorldGameplayCommitterTests, RoundResetRemovesDeathDropAlongsideAmbientResources)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity deadPlayer = CreatePlayer(10, &entityManager, &state);
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);

        const WorldResourceSlotState collectedAmbientSlot = state.ResourceSlots()[0];
        const WorldGameplayPhaseResult pickupResult =
            MakePhaseResult(50, {MakePickup(state, collectedAmbientSlot, deadPlayer, 1)},
                            {WorldPlayerScoreAward{deadPlayer.playerId, deadPlayer.key, 1}});
        WorldGameplayCommitReport pickupReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, pickupResult, entityManager, state, &pickupReport),
                  WorldGameplayCommitResult::Committed);

        const WorldGameplayPhaseResult deathResult{
            51,
            {},
            {},
            {},
            WorldGameplayRoundTransition{},
            {},
            {WorldPlayerDeathCommit{deadPlayer.playerId, deadPlayer.key, WorldPlayerDeathCauseFlags::Collision}},
            {WorldResourceSpawnRequest{deadPlayer.key, 0, 10.0f, 10.0f}},
        };
        WorldGameplayCommitReport deathReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, deathResult, entityManager, state, &deathReport),
                  WorldGameplayCommitResult::Committed);
        ASSERT_EQ(deathReport.spawnedResourceKeys.size(), 1u);
        const WorldEntityKey deathDropKey = deathReport.spawnedResourceKeys[0];

        const WorldGameplayPhaseResult endedResult{
            100,
            {},
            {},
            {},
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Ended,
                WorldRoundRuntimeState{1, WorldRoundPhase::Ended, 160, 0},
            },
        };
        WorldGameplayCommitReport endedReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, endedResult, entityManager, state, &endedReport),
                  WorldGameplayCommitResult::Committed);
        const WorldGameplayPhaseResult resetResult{
            160,
            {},
            {},
            MakeAllSpawnRequests(config, state, 160),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::ResetToRunning,
                WorldRoundRuntimeState{2, WorldRoundPhase::Running, 1360, 0},
            },
        };
        WorldGameplayCommitReport resetReport;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, resetResult, entityManager, state, &resetReport),
                  WorldGameplayCommitResult::Committed);

        WorldResourceInstance removedDeathDrop;
        EntityHandle removedDeathDropHandle;
        EXPECT_FALSE(state.ResourceRegistry().TryFind(deathDropKey, &removedDeathDrop));
        EXPECT_FALSE(entityManager.TryFindHandle(deathDropKey, &removedDeathDropHandle));
        EXPECT_EQ(state.ResourceRegistry().Count(), state.ResourceSlots().size());
        EXPECT_NE(std::find(resetReport.entityRemovals.begin(), resetReport.entityRemovals.end(),
                            WorldGameplayEntityRemoval{deathDropKey, WorldGameplayEntityRemoveReason::RoundReset}),
                  resetReport.entityRemovals.end());
    }

    TEST(WorldGameplayCommitterTests, CommitsPlayerSpawnWithEntityAndSessionRebind)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity player = CreatePlayer(10, &entityManager, &state);
        const WorldSessionKey sessionKey{100};
        WorldSessionRegistry sessionRegistry;
        ASSERT_TRUE(sessionRegistry.TryRegister(sessionKey));
        ASSERT_TRUE(sessionRegistry.TryBindPlayer(sessionKey, player.playerId, player.key));

        const WorldGameplayPhaseResult deathResult{
            50,
            {},
            {},
            {},
            WorldGameplayRoundTransition{},
            {},
            {WorldPlayerDeathCommit{player.playerId, player.key, WorldPlayerDeathCauseFlags::Collision}},
        };
        WorldGameplayCommitReport deathReport;
        ASSERT_EQ(WorldGameplayCommitter::CommitPickups(config, deathResult, entityManager, state, &deathReport),
                  WorldGameplayCommitResult::Committed);

        constexpr WorldPlayerSpawnPlannerConfig SpawnConfig{
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
            4,
            1,
            5.0f,
            WorldPlayerBodyConfig{WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f}, 16},
        };
        WorldResult<WorldPlayerSpawnCandidate> candidateResult =
            WorldPlayerSpawnPlanner::Plan(SpawnConfig, 51, player.playerId, 0);
        ASSERT_TRUE(candidateResult.Succeeded());
        const WorldPlayerSpawnCandidate candidate = candidateResult.TakeValue();
        WorldGameplayCommitReport spawnReport;
        spawnReport.serverTick = 51;

        ASSERT_EQ(WorldGameplayCommitter::CommitPlayerSpawns(std::span<const WorldPlayerSpawnCandidate>{&candidate, 1},
                                                             entityManager, state, sessionRegistry, &spawnReport),
                  WorldGameplayCommitResult::Committed);

        WorldPlayerScore score;
        WorldSession session;
        WorldEntityComponents spawnedComponents;
        EntityHandle spawnedHandle;
        ASSERT_TRUE(state.TryFindPlayerScore(player.playerId, &score));
        ASSERT_TRUE(sessionRegistry.TryFind(sessionKey, &session));
        ASSERT_TRUE(entityManager.TryFindHandle(score.controlledEntityKey, &spawnedHandle));
        ASSERT_TRUE(entityManager.TryReadComponents(spawnedHandle, &spawnedComponents));
        EXPECT_EQ(score.lifecycle, WorldPlayerLifecycle::Alive);
        EXPECT_EQ(score.score, 0u);
        EXPECT_EQ(score.boostCostAccumulator, 0.0f);
        EXPECT_NE(score.controlledEntityKey, player.key);
        EXPECT_EQ(session.entityKey, score.controlledEntityKey);
        EXPECT_EQ(spawnedComponents, candidate.components);
        ASSERT_EQ(spawnReport.playerSpawns.size(), 1u);
        EXPECT_EQ(spawnReport.playerSpawns[0],
                  (WorldGameplayPlayerSpawn{player.playerId, sessionKey, player.key, score.controlledEntityKey}));
        EXPECT_TRUE(spawnReport.forceAoiReplication);
    }

    TEST(WorldGameplayCommitterTests, InvalidMappingDoesNotApplyPartialMutation)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity player = CreatePlayer(10, &entityManager, &state);
        ASSERT_EQ(StartRoundWithResources(config, 1, entityManager, state), WorldGameplayCommitResult::Committed);
        const WorldResourceSlotState resource = state.ResourceSlots()[0];
        const WorldResourceInstance resourceInstance = FindAmbientResource(state, resource.slotId);
        WorldResourcePickup invalidPickup = MakePickup(state, resource, player, config.resourceScoreValue);
        invalidPickup.resourceEntityHandle.slotGeneration += 1;
        const WorldGameplayPhaseResult invalidResult = MakePhaseResult(
            50, {invalidPickup}, {WorldPlayerScoreAward{player.playerId, player.key, config.resourceScoreValue}});
        WorldGameplayCommitReport report;

        EXPECT_EQ(WorldGameplayCommitter::CommitPickups(config, invalidResult, entityManager, state, &report),
                  WorldGameplayCommitResult::StateInvariantViolation);

        WorldPlayerScore score;
        EntityHandle currentHandle;
        ASSERT_TRUE(state.TryFindPlayerScore(player.playerId, &score));
        EXPECT_EQ(score.score, 0u);
        EXPECT_EQ(state.ResourceSlots()[0], resource);
        EXPECT_TRUE(entityManager.TryFindHandle(resourceInstance.entityKey, &currentHandle));
        EXPECT_EQ(currentHandle, resourceInstance.entityHandle);
        EXPECT_TRUE(report.entityRemovals.empty());
        EXPECT_TRUE(report.scoreSnapshots.empty());
    }

    TEST(WorldGameplayCommitterTests, CommitsWaitingStartAndForcedResourceSpawns)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        static_cast<void>(CreatePlayer(10, &entityManager, &state));
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        const WorldGameplayPhaseResult phaseResult{
            50,
            {},
            {},
            MakeAllSpawnRequests(config, state, 50),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Started,
                WorldRoundRuntimeState{1, WorldRoundPhase::Running, 1250, 0},
            },
        };
        WorldGameplayCommitReport report;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, phaseResult, entityManager, state, &report),
                  WorldGameplayCommitResult::Committed);

        EXPECT_EQ(state.RoundState(), (WorldRoundRuntimeState{1, WorldRoundPhase::Running, 1250, 0}));
        EXPECT_TRUE(report.roundSnapshotChanged);
        EXPECT_EQ(report.roundSnapshot, state.RoundState());
        EXPECT_EQ(report.resourceSpawnCount, 2u);
        EXPECT_EQ(report.spawnedResourceKeys.size(), 2u);
        EXPECT_TRUE(report.forceAoiReplication);
        for (const WorldResourceSlotState& slot : state.ResourceSlots())
        {
            WorldResourceInstance resourceInstance;
            EXPECT_EQ(slot.phase, WorldResourceSlotPhase::Active);
            EXPECT_TRUE(state.ResourceRegistry().TryFindAmbientSlot(slot.slotId, &resourceInstance));
        }
    }

    TEST(WorldGameplayCommitterTests, InvalidSecondSpawnRequestDoesNotCreateOrphanEntity)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        static_cast<void>(CreatePlayer(10, &entityManager, &state));
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        const std::size_t entityCount = entityManager.Size();
        std::vector<WorldResourceSpawnRequest> spawnRequests = MakeAllSpawnRequests(config, state, 50);
        spawnRequests[1].positionX = std::numeric_limits<float>::quiet_NaN();
        const WorldGameplayPhaseResult phaseResult{
            50,
            {},
            {},
            std::move(spawnRequests),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Started,
                WorldRoundRuntimeState{1, WorldRoundPhase::Running, 1250, 0},
            },
        };
        WorldGameplayCommitReport report;
        report.serverTick = 777;

        EXPECT_EQ(WorldGameplayCommitter::Commit(config, phaseResult, entityManager, state, &report),
                  WorldGameplayCommitResult::StateInvariantViolation);

        EXPECT_EQ(entityManager.Size(), entityCount);
        EXPECT_EQ(state.ResourceRegistry().Count(), 0u);
        EXPECT_EQ(report.serverTick, 777u);
        for (const WorldResourceSlotState& slot : state.ResourceSlots())
        {
            EXPECT_EQ(slot.phase, WorldResourceSlotPhase::Dormant);
        }
    }

    TEST(WorldGameplayCommitterTests, SameTickAmbientRefillUsesNewEntityIdentity)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        const PlayerEntity player = CreatePlayer(10, &entityManager, &state);
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        const WorldGameplayPhaseResult startResult{
            10,
            {},
            {},
            MakeAllSpawnRequests(config, state, 10),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Started,
                WorldRoundRuntimeState{1, WorldRoundPhase::Running, 1210, 0},
            },
        };
        WorldGameplayCommitReport startReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, startResult, entityManager, state, &startReport),
                  WorldGameplayCommitResult::Committed);
        ASSERT_EQ(state.ResourceRegistry().Count(), 2u);
        const WorldResourceSlotState collected = state.ResourceSlots()[0];
        const WorldResourceInstance collectedInstance = FindAmbientResource(state, collected.slotId);
        const WorldResourceSpawnRequest refillRequest =
            MakeSpawnRequest(config, 50, collected.slotId, state.ResourceRegistry(), {});
        const WorldGameplayPhaseResult refillResult{
            50,
            {MakePickup(state, collected, player, 1)},
            {WorldPlayerScoreAward{player.playerId, player.key, 1}},
            {refillRequest},
            WorldGameplayRoundTransition{},
        };
        WorldGameplayCommitReport refillReport;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, refillResult, entityManager, state, &refillReport),
                  WorldGameplayCommitResult::Committed);

        const WorldResourceSlotState& respawned = state.ResourceSlots()[0];
        EXPECT_EQ(respawned.phase, WorldResourceSlotPhase::Active);
        const WorldResourceInstance respawnedInstance = FindAmbientResource(state, respawned.slotId);
        EXPECT_NE(respawnedInstance.entityKey, collectedInstance.entityKey);
        EXPECT_TRUE(respawnedInstance.positionX != collectedInstance.positionX ||
                    respawnedInstance.positionY != collectedInstance.positionY);
        EXPECT_TRUE(MakeActiveArea().ContainsCircleStrictly(respawnedInstance.positionX, respawnedInstance.positionY,
                                                            config.resourceCircleRadius));
        EXPECT_EQ(respawnedInstance.ambientSlotId, respawned.slotId);
        EXPECT_EQ(state.ResourceRegistry().Count(), 2u);
        EXPECT_EQ(refillReport.spawnedResourceKeys, std::vector<WorldEntityKey>{respawnedInstance.entityKey});
        EXPECT_TRUE(refillReport.forceAoiReplication);
    }

    TEST(WorldGameplayCommitterTests, OutsideAmbientCleanupCanRefillSameSlot)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        static_cast<void>(CreatePlayer(10, &entityManager, &state));
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        ASSERT_EQ(StartRoundWithResources(config, 10, entityManager, state), WorldGameplayCommitResult::Committed);
        const WorldResourceInstance outsideResource = FindAmbientResource(state, state.ResourceSlots()[0].slotId);
        const WorldResourceSpawnRequest refillRequest =
            MakeSpawnRequest(config, 50, outsideResource.ambientSlotId, state.ResourceRegistry(), {});
        const WorldGameplayPhaseResult phaseResult{
            50, {}, {}, {refillRequest}, WorldGameplayRoundTransition{}, {}, {}, {}, 0, {outsideResource},
        };
        WorldGameplayCommitReport report;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, phaseResult, entityManager, state, &report),
                  WorldGameplayCommitResult::Committed);

        WorldResourceInstance removedResource;
        EntityHandle removedHandle;
        EXPECT_FALSE(state.ResourceRegistry().TryFind(outsideResource.entityKey, &removedResource));
        EXPECT_FALSE(entityManager.TryFindHandle(outsideResource.entityKey, &removedHandle));
        const WorldResourceInstance replacement = FindAmbientResource(state, outsideResource.ambientSlotId);
        EXPECT_NE(replacement.entityKey, outsideResource.entityKey);
        EXPECT_EQ(state.ResourceSlots()[0].phase, WorldResourceSlotPhase::Active);
        ASSERT_EQ(report.entityRemovals.size(), 1u);
        EXPECT_EQ(report.entityRemovals[0],
                  (WorldGameplayEntityRemoval{outsideResource.entityKey,
                                              WorldGameplayEntityRemoveReason::OutsideActiveArea}));
        EXPECT_TRUE(report.forceAoiReplication);
    }

    TEST(WorldGameplayCommitterTests, RoundResetRemovesOldResourcesAndStartsNewGeneration)
    {
        const WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        static_cast<void>(CreatePlayer(10, &entityManager, &state));
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        const WorldGameplayPhaseResult startResult{
            50,
            {},
            {},
            MakeAllSpawnRequests(config, state, 50),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Started,
                WorldRoundRuntimeState{1, WorldRoundPhase::Running, 1250, 0},
            },
        };
        WorldGameplayCommitReport startReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, startResult, entityManager, state, &startReport),
                  WorldGameplayCommitResult::Committed);
        const std::vector<WorldEntityKey> oldKeys(startReport.spawnedResourceKeys);
        ASSERT_EQ(state.ResourceRegistry().Count(), oldKeys.size());
        const WorldGameplayPhaseResult endedResult{
            100,
            {},
            {},
            {},
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Ended,
                WorldRoundRuntimeState{1, WorldRoundPhase::Ended, 160, 0},
            },
        };
        WorldGameplayCommitReport endedReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, endedResult, entityManager, state, &endedReport),
                  WorldGameplayCommitResult::Committed);
        const WorldGameplayPhaseResult resetResult{
            160,
            {},
            {},
            MakeAllSpawnRequests(config, state, 160),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::ResetToRunning,
                WorldRoundRuntimeState{2, WorldRoundPhase::Running, 1360, 0},
            },
        };
        WorldGameplayCommitReport resetReport;

        ASSERT_EQ(WorldGameplayCommitter::Commit(config, resetResult, entityManager, state, &resetReport),
                  WorldGameplayCommitResult::Committed);

        EXPECT_EQ(state.RoundState(), (WorldRoundRuntimeState{2, WorldRoundPhase::Running, 1360, 0}));
        EXPECT_EQ(state.ResourceRegistry().Count(), resetReport.spawnedResourceKeys.size());
        ASSERT_EQ(resetReport.entityRemovals.size(), oldKeys.size());
        ASSERT_EQ(resetReport.spawnedResourceKeys.size(), oldKeys.size());
        for (std::size_t index = 0; index < oldKeys.size(); ++index)
        {
            WorldResourceInstance oldInstance;
            WorldResourceInstance newInstance;
            EXPECT_FALSE(state.ResourceRegistry().TryFind(oldKeys[index], &oldInstance));
            EXPECT_TRUE(state.ResourceRegistry().TryFind(resetReport.spawnedResourceKeys[index], &newInstance));
            EXPECT_EQ(resetReport.entityRemovals[index],
                      (WorldGameplayEntityRemoval{oldKeys[index], WorldGameplayEntityRemoveReason::RoundReset}));
            EXPECT_NE(resetReport.spawnedResourceKeys[index], oldKeys[index]);
        }
    }

    TEST(WorldGameplayCommitterTests, RegistryConflictDuringRoundResetPreservesExistingResources)
    {
        WorldGameplayConfig config = MakeConfig();
        WorldGameplayState state = MakeState(config);
        WorldEntityManager entityManager;
        static_cast<void>(CreatePlayer(10, &entityManager, &state));
        static_cast<void>(CreatePlayer(20, &entityManager, &state));
        const WorldGameplayPhaseResult startResult{
            50,
            {},
            {},
            MakeAllSpawnRequests(config, state, 50),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Started,
                WorldRoundRuntimeState{1, WorldRoundPhase::Running, 1250, 0},
            },
        };
        WorldGameplayCommitReport startReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, startResult, entityManager, state, &startReport),
                  WorldGameplayCommitResult::Committed);
        const WorldGameplayPhaseResult endedResult{
            100,
            {},
            {},
            {},
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::Ended,
                WorldRoundRuntimeState{1, WorldRoundPhase::Ended, 160, 0},
            },
        };
        WorldGameplayCommitReport endedReport;
        ASSERT_EQ(WorldGameplayCommitter::Commit(config, endedResult, entityManager, state, &endedReport),
                  WorldGameplayCommitResult::Committed);

        const WorldRoundRuntimeState roundBeforeReset = state.RoundState();
        const std::vector<WorldResourceSlotState> slotsBeforeReset(state.ResourceSlots().begin(),
                                                                   state.ResourceSlots().end());
        const std::size_t entityCountBeforeReset = entityManager.Size();
        std::vector<WorldResourceInstance> resourcesBeforeReset;
        for (const WorldResourceSlotState& slot : slotsBeforeReset)
        {
            resourcesBeforeReset.push_back(FindAmbientResource(state, slot.slotId));
        }

        std::vector<WorldResourceSpawnRequest> conflictingSpawns = MakeAllSpawnRequests(config, state, 160);
        conflictingSpawns[1].positionX = conflictingSpawns[0].positionX;
        conflictingSpawns[1].positionY = conflictingSpawns[0].positionY;
        const WorldGameplayPhaseResult resetResult{
            160,
            {},
            {},
            std::move(conflictingSpawns),
            WorldGameplayRoundTransition{
                WorldGameplayRoundTransitionKind::ResetToRunning,
                WorldRoundRuntimeState{2, WorldRoundPhase::Running, 1360, 0},
            },
        };
        WorldGameplayCommitReport unchangedReport;
        unchangedReport.serverTick = 777;

        EXPECT_EQ(WorldGameplayCommitter::Commit(config, resetResult, entityManager, state, &unchangedReport),
                  WorldGameplayCommitResult::StateInvariantViolation);

        EXPECT_EQ(state.RoundState(), roundBeforeReset);
        EXPECT_EQ(std::vector<WorldResourceSlotState>(state.ResourceSlots().begin(), state.ResourceSlots().end()),
                  slotsBeforeReset);
        EXPECT_EQ(state.ResourceRegistry().Count(), resourcesBeforeReset.size());
        EXPECT_EQ(entityManager.Size(), entityCountBeforeReset);
        EXPECT_EQ(unchangedReport.serverTick, 777u);
        for (const WorldResourceInstance& resource : resourcesBeforeReset)
        {
            WorldResourceInstance currentResource;
            EntityHandle currentHandle;
            ASSERT_TRUE(state.ResourceRegistry().TryFind(resource.entityKey, &currentResource));
            EXPECT_EQ(currentResource, resource);
            ASSERT_TRUE(entityManager.TryFindHandle(resource.entityKey, &currentHandle));
            EXPECT_EQ(currentHandle, resource.entityHandle);
        }
    }
} // namespace psnr::world::tests
