#include "pch.h"

#include "WorldTickProcessor.h"

#include <numbers>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakeComponents(const std::uint32_t playerId)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{10.0f, 20.0f, 0.5f};
            components.motion = MotionComponent{};
            components.movementCapability = MovementCapabilityComponent{10.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 100, WorldShapeKind::Circle, 0.75f};
            components.playerControl = PlayerControlComponent{playerId};
            return components;
        }

        [[nodiscard]] WorldMovementCommand MakeCommand(const WorldSession& session,
                                                       const std::uint32_t targetServerTick, const float movementInputX,
                                                       const float movementInputY)
        {
            return WorldMovementCommand{
                session.sessionKey, session.playerId, session.entityKey, targetServerTick,
                targetServerTick,   movementInputX,   movementInputY,
            };
        }

        [[nodiscard]] WorldEntityComponents MakeWorldObject(const WorldEntityKind kind, const float positionX,
                                                            const float radius)
        {
            WorldEntityComponents components;
            components.transform.positionX = positionX;
            components.replicationMetadata = ReplicationMetadataComponent{kind, 200, WorldShapeKind::Circle, radius};
            return components;
        }
    } // namespace

    TEST(WorldTickProcessorTests, ProcessesImmutableMovementInputAndCommitsCanonicalState)
    {
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor;
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents actual;

        ASSERT_TRUE(entityManager.TryCreate(MakeComponents(7), &entityKey, &handle));
        const WorldSession session{WorldSessionKey{10}, 7, entityKey};
        const std::vector<WorldSession> sessions{session};
        ASSERT_EQ(commandStore.TryStore(MakeCommand(session, 100, 1.0f, -0.5f)),
                  WorldMovementCommandStoreResult::Stored);

        ASSERT_EQ(processor.Process(100, 0.05f, sessions, commandStore, entityManager),
                  WorldTickProcessResult::Processed);

        ASSERT_TRUE(entityManager.TryReadComponents(handle, &actual));
        EXPECT_FLOAT_EQ(actual.transform.positionX, 10.5f);
        EXPECT_FLOAT_EQ(actual.transform.positionY, 19.75f);
        EXPECT_FLOAT_EQ(actual.transform.angleRadians, 0.5f);
        EXPECT_FLOAT_EQ(actual.motion.velocityX, 10.0f);
        EXPECT_FLOAT_EQ(actual.motion.velocityY, -5.0f);
        EXPECT_FLOAT_EQ(actual.motion.movementIntentX, 1.0f);
        EXPECT_FLOAT_EQ(actual.motion.movementIntentY, -0.5f);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldTickProcessorTests, RejectsInvalidDeltaWithoutConsumingCommandOrMutatingState)
    {
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor;
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents actual;
        const WorldEntityComponents original = MakeComponents(7);

        ASSERT_TRUE(entityManager.TryCreate(original, &entityKey, &handle));
        const WorldSession session{WorldSessionKey{10}, 7, entityKey};
        const std::vector<WorldSession> sessions{session};
        ASSERT_EQ(commandStore.TryStore(MakeCommand(session, 100, 1.0f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);

        EXPECT_EQ(processor.Process(100, 0.0f, sessions, commandStore, entityManager),
                  WorldTickProcessResult::InvalidFixedDelta);
        EXPECT_EQ(commandStore.Size(), 1u);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &actual));
        EXPECT_EQ(actual, original);
    }

    TEST(WorldTickProcessorTests, RejectsNonSequentialTickBeforeConsumingItsCommand)
    {
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor;
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents actual;

        ASSERT_TRUE(entityManager.TryCreate(MakeComponents(7), &entityKey, &handle));
        const WorldSession session{WorldSessionKey{10}, 7, entityKey};
        const std::vector<WorldSession> sessions{session};
        ASSERT_EQ(commandStore.TryStore(MakeCommand(session, 100, 1.0f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(processor.Process(100, 0.05f, sessions, commandStore, entityManager),
                  WorldTickProcessResult::Processed);
        ASSERT_EQ(commandStore.TryStore(MakeCommand(session, 102, -1.0f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);

        EXPECT_EQ(processor.Process(102, 0.05f, sessions, commandStore, entityManager),
                  WorldTickProcessResult::NonSequentialServerTick);
        EXPECT_EQ(commandStore.Size(), 1u);

        ASSERT_EQ(processor.Process(101, 0.05f, sessions, commandStore, entityManager),
                  WorldTickProcessResult::Processed);
        ASSERT_EQ(processor.Process(102, 0.05f, sessions, commandStore, entityManager),
                  WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &actual));
        EXPECT_FLOAT_EQ(actual.transform.positionX, 10.5f);
        EXPECT_FLOAT_EQ(actual.motion.velocityX, -10.0f);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldTickProcessorTests, DoubleBufferedResumesAfterPausedTickWithoutReplayingInput)
    {
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor;
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents afterInput;
        WorldEntityComponents afterEmptyEpoch;

        ASSERT_TRUE(entityManager.TryCreate(MakeComponents(7), &entityKey, &handle));
        const WorldSession session{WorldSessionKey{10}, 7, entityKey};
        const std::vector<WorldSession> sessions{session};
        ASSERT_EQ(commandStore.TryStore(WorldInboundMode::DoubleBuffered, MakeCommand(session, 100, 1.0f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);

        ASSERT_EQ(
            processor.Process(WorldInboundMode::DoubleBuffered, 100, 0.05f, sessions, commandStore, entityManager),
            WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &afterInput));
        EXPECT_FLOAT_EQ(afterInput.transform.positionX, 10.5f);

        ASSERT_EQ(
            processor.Process(WorldInboundMode::DoubleBuffered, 102, 0.05f, sessions, commandStore, entityManager),
            WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &afterEmptyEpoch));
        EXPECT_EQ(afterEmptyEpoch, afterInput);
    }

    TEST(WorldTickProcessorTests, AppliesControlHeadingAndGatesBoostByGrowthPoint)
    {
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{},
            WorldPhysicsArenaBounds{},
            WorldControlMovementConfig{5.0f, 7.5f, std::numbers::pi_v<float>},
        };
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};
        WorldEntityComponents components = MakeComponents(7);
        components.transform.angleRadians = 0.0f;
        components.playerControl.lastInputSequence = 1;
        components.playerControl.turnState = WorldTurnState::Right;
        components.playerControl.boostState = WorldBoostState::On;
        WorldEntityKey entityKey;
        EntityHandle handle;
        ASSERT_TRUE(entityManager.TryCreate(components, &entityKey, &handle));
        const WorldSession session{WorldSessionKey{10}, 7, entityKey};
        const std::vector<WorldSession> sessions{session};
        std::vector<WorldPlayerScore> scores{WorldPlayerScore{7, entityKey, 0}};

        ASSERT_EQ(processor.Process(100, 0.5f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &components));
        EXPECT_NEAR(components.transform.angleRadians, -std::numbers::pi_v<float> / 2.0f, 0.000001f);
        EXPECT_NEAR(components.transform.positionX, 10.0f, 0.000001f);
        EXPECT_NEAR(components.transform.positionY, 17.5f, 0.000001f);
        EXPECT_NEAR(components.motion.velocityY, -5.0f, 0.000001f);

        components.playerControl.turnState = WorldTurnState::Straight;
        ASSERT_TRUE(entityManager.TryReplaceComponents(handle, components));
        scores[0].score = 1;
        ASSERT_EQ(processor.Process(101, 0.5f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &components));
        EXPECT_NEAR(components.transform.angleRadians, -std::numbers::pi_v<float> / 2.0f, 0.000001f);
        EXPECT_NEAR(components.transform.positionY, 13.75f, 0.000001f);
        EXPECT_NEAR(components.motion.velocityY, -7.5f, 0.000001f);
    }

    TEST(WorldTickProcessorTests, RecordsCommittedHeadAtBodyTrailCadence)
    {
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{},
            WorldPhysicsArenaBounds{},
            WorldControlMovementConfig{5.0f, 7.5f, std::numbers::pi_v<float>},
            WorldBodyTrailSampleConfig{3, 560},
        };
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};
        WorldEntityComponents components = MakeComponents(7);
        components.transform.angleRadians = 0.0f;
        WorldEntityKey entityKey;
        EntityHandle handle;
        ASSERT_TRUE(entityManager.TryCreate(components, &entityKey, &handle));
        const WorldSession session{WorldSessionKey{10}, 7, entityKey};
        const std::vector<WorldSession> sessions{session};
        const std::vector<WorldPlayerScore> scores{WorldPlayerScore{7, entityKey, 0}};

        ASSERT_EQ(processor.Process(100, 0.1f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_EQ(processor.Process(101, 0.1f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &components));
        EXPECT_TRUE(components.bodyTrail.Empty());

        ASSERT_EQ(processor.Process(102, 0.1f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_TRUE(entityManager.TryReadComponents(handle, &components));
        ASSERT_EQ(components.bodyTrail.SampleCount(), 1u);
        BodyTrailSample sample;
        ASSERT_TRUE(components.bodyTrail.TryRead(0, &sample));
        EXPECT_FLOAT_EQ(sample.positionX, 11.5f);
        EXPECT_FLOAT_EQ(sample.positionY, 20.0f);
    }

    TEST(WorldTickProcessorTests, PhysicsIntegrationCommitsCollisionAndExposesTriggerEvidence)
    {
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{WorldPhysicsMaxContactsPerTick, 0.0001f, 0.0001f},
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
            WorldControlMovementConfig{10.0f, 15.0f, std::numbers::pi_v<float>},
        };
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};
        WorldEntityComponents playerComponents = MakeComponents(7);
        playerComponents.transform = TransformComponent{};
        WorldEntityKey playerKey;
        EntityHandle playerHandle;
        WorldEntityKey obstacleKey;
        EntityHandle obstacleHandle;
        WorldEntityKey resourceKey;
        EntityHandle resourceHandle;
        ASSERT_TRUE(entityManager.TryCreate(playerComponents, &playerKey, &playerHandle));
        ASSERT_TRUE(entityManager.TryCreate(MakeWorldObject(WorldEntityKind::StaticObstacle, 6.0f, 1.0f), &obstacleKey,
                                            &obstacleHandle));
        ASSERT_TRUE(entityManager.TryCreate(MakeWorldObject(WorldEntityKind::Resource, 4.25f, 0.5f), &resourceKey,
                                            &resourceHandle));

        const WorldSession session{WorldSessionKey{10}, 7, playerKey};
        const std::vector<WorldSession> sessions{session};
        const std::vector<WorldPlayerScore> scores{WorldPlayerScore{7, playerKey, 0}};
        ASSERT_EQ(commandStore.TryStore(MakeCommand(session, 100, 1.0f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(processor.Process(100, 1.0f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);

        WorldEntityComponents actual;
        ASSERT_TRUE(entityManager.TryReadComponents(playerHandle, &actual));
        EXPECT_NEAR(actual.transform.positionX, 4.25f, 0.0001f);
        EXPECT_NEAR(actual.motion.velocityX, 4.25f, 0.0001f);
        ASSERT_EQ(processor.LastPhysicsResult().Contacts().size(), 1u);
        EXPECT_EQ(processor.LastPhysicsResult().Contacts()[0].collider.entityProxy.ownerKey, obstacleKey);
        ASSERT_EQ(processor.LastPhysicsResult().TriggerOverlaps().size(), 1u);
        EXPECT_EQ(processor.LastPhysicsResult().TriggerOverlaps()[0].second.ownerKey, resourceKey);
    }

    TEST(WorldTickProcessorTests, ProducesCollisionDeathSetFromTrimmedPlayerBodies)
    {
        constexpr WorldGrowthConfig GrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldPlayerBodyConfig PlayerBodyConfig{GrowthConfig, 16};
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{WorldPhysicsMaxContactsPerTick, 0.0001f, 0.0001f},
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
            WorldControlMovementConfig{},
            WorldBodyTrailSampleConfig{},
            PlayerBodyConfig,
        };
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};

        WorldEntityComponents firstComponents = MakeComponents(7);
        firstComponents.transform = TransformComponent{0.0f, 0.0f, 0.0f};
        ASSERT_EQ(WorldPlayerBody::Initialize(PlayerBodyConfig, &firstComponents),
                  WorldPlayerBodyUpdateResult::Updated);
        WorldEntityComponents secondComponents = MakeComponents(8);
        secondComponents.transform = TransformComponent{5.0f, 0.0f, 0.0f};
        ASSERT_EQ(WorldPlayerBody::Initialize(PlayerBodyConfig, &secondComponents),
                  WorldPlayerBodyUpdateResult::Updated);

        WorldEntityKey firstKey;
        EntityHandle firstHandle;
        WorldEntityKey secondKey;
        EntityHandle secondHandle;
        ASSERT_TRUE(entityManager.TryCreate(firstComponents, &firstKey, &firstHandle));
        ASSERT_TRUE(entityManager.TryCreate(secondComponents, &secondKey, &secondHandle));
        const std::vector<WorldSession> sessions{
            WorldSession{WorldSessionKey{10}, 7, firstKey},
            WorldSession{WorldSessionKey{11}, 8, secondKey},
        };
        const std::vector<WorldPlayerScore> scores{
            WorldPlayerScore{7, firstKey, 0},
            WorldPlayerScore{8, secondKey, 0},
        };

        ASSERT_EQ(processor.Process(100, 0.05f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_EQ(processor.LastCollisionDeathSet().size(), 1u);
        EXPECT_EQ(processor.LastCollisionDeathSet()[0], firstKey);
    }

    TEST(WorldTickProcessorTests, ChecksActiveAreaOnlyAfterMovement)
    {
        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 10.0f, 1.0f, 10.0f};
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor;
        WorldEntityComponents returningComponents = MakeComponents(7);
        returningComponents.transform = TransformComponent{10.0f, 0.0f, 0.0f};
        WorldEntityComponents outsideComponents = MakeComponents(8);
        outsideComponents.transform = TransformComponent{20.0f, 0.0f, 0.0f};
        WorldEntityKey returningKey;
        EntityHandle returningHandle;
        WorldEntityKey outsideKey;
        EntityHandle outsideHandle;
        ASSERT_TRUE(entityManager.TryCreate(returningComponents, &returningKey, &returningHandle));
        ASSERT_TRUE(entityManager.TryCreate(outsideComponents, &outsideKey, &outsideHandle));
        const WorldSession returningSession{WorldSessionKey{10}, 7, returningKey};
        const std::vector<WorldSession> sessions{
            returningSession,
            WorldSession{WorldSessionKey{11}, 8, outsideKey},
        };
        const std::vector<WorldPlayerScore> scores{
            WorldPlayerScore{7, returningKey, 0},
            WorldPlayerScore{8, outsideKey, 0},
        };
        ASSERT_EQ(commandStore.TryStore(MakeCommand(returningSession, 100, -1.0f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);

        ASSERT_EQ(processor.Process(100, 0.1f, sessions, commandStore, entityManager, scores, &ActiveArea),
                  WorldTickProcessResult::Processed);

        ASSERT_TRUE(entityManager.TryReadComponents(returningHandle, &returningComponents));
        EXPECT_FLOAT_EQ(returningComponents.transform.positionX, 9.0f);
        ASSERT_EQ(processor.LastActiveAreaBoundaryDeathSet().size(), 1u);
        EXPECT_EQ(processor.LastActiveAreaBoundaryDeathSet()[0], outsideKey);
    }

    TEST(WorldTickProcessorTests, PlansSpawnCandidateForPendingPlayerWithoutRequiringRemovedEntity)
    {
        constexpr WorldGrowthConfig GrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{WorldPhysicsMaxContactsPerTick, 0.0001f, 0.0001f},
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
            WorldControlMovementConfig{5.0f, 7.5f, std::numbers::pi_v<float>},
            WorldBodyTrailSampleConfig{},
            WorldPlayerBodyConfig{GrowthConfig, 16},
            1,
            4,
        };
        const WorldEntityKey removedEntityKey{1, 1};
        const std::vector<WorldSession> sessions{
            WorldSession{WorldSessionKey{10}, 7, removedEntityKey},
        };
        const std::vector<WorldPlayerScore> scores{
            WorldPlayerScore{7, removedEntityKey, 0, 0.0f, WorldPlayerLifecycle::SpawnPending},
        };
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};

        EXPECT_EQ(processor.Process(100, 0.05f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        EXPECT_TRUE(processor.LastCollisionDeathSet().empty());
        ASSERT_EQ(processor.LastPlayerSpawnCandidates().size(), 1u);
        EXPECT_EQ(processor.LastPlayerSpawnCandidates()[0].playerId, 7u);
        EXPECT_EQ(processor.LastPlayerSpawnCandidates()[0].ordinal, 0u);
        EXPECT_EQ(entityManager.Size(), 0u);
    }

    TEST(WorldTickProcessorTests, KeepsPendingPlayerWhenInitialBodyDoesNotFitInsideActiveArea)
    {
        constexpr WorldGrowthConfig GrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{WorldPhysicsMaxContactsPerTick, 0.0001f, 0.0001f},
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
            WorldControlMovementConfig{5.0f, 7.5f, std::numbers::pi_v<float>},
            WorldBodyTrailSampleConfig{},
            WorldPlayerBodyConfig{GrowthConfig, 16},
            1,
            1,
        };
        constexpr WorldPlayerSpawnPlannerConfig SpawnConfig{
            Config.arenaBounds,       Config.playerSpawnMaxCandidatesPerTick,
            Config.playerArchetypeId, Config.controlMovement.baseSpeed,
            Config.playerBody,
        };
        constexpr std::uint32_t ServerTick = 100;
        constexpr std::uint32_t PlayerId = 7;
        WorldResult<WorldPlayerSpawnCandidate> candidateResult =
            WorldPlayerSpawnPlanner::Plan(SpawnConfig, ServerTick, PlayerId, 0);
        ASSERT_TRUE(candidateResult.Succeeded());
        const WorldPlayerSpawnCandidate candidate = candidateResult.TakeValue();
        const float headRadius = candidate.components.replicationMetadata.primaryCircleRadius;
        const float activeRadius = headRadius + 1.0f;
        const WorldActiveArea headOnlyActiveArea{
            candidate.components.transform.positionX,
            candidate.components.transform.positionY,
            activeRadius,
            1.0f,
            activeRadius,
        };
        ASSERT_TRUE(headOnlyActiveArea.ContainsCircleStrictly(candidate.components.transform.positionX,
                                                              candidate.components.transform.positionY, headRadius));
        bool hasOutsideBodySample = false;
        for (std::size_t index = 0; index < candidate.components.bodyTrail.SampleCount(); ++index)
        {
            BodyTrailSample sample;
            ASSERT_TRUE(candidate.components.bodyTrail.TryRead(index, &sample));
            if (!headOnlyActiveArea.ContainsCircleStrictly(sample.positionX, sample.positionY, headRadius))
            {
                hasOutsideBodySample = true;
                break;
            }
        }
        ASSERT_TRUE(hasOutsideBodySample);

        const WorldEntityKey removedEntityKey{1, 1};
        const std::vector<WorldSession> sessions{
            WorldSession{WorldSessionKey{10}, PlayerId, removedEntityKey},
        };
        const std::vector<WorldPlayerScore> scores{
            WorldPlayerScore{PlayerId, removedEntityKey, 0, 0.0f, WorldPlayerLifecycle::SpawnPending},
        };
        WorldEntityManager entityManager;
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};

        EXPECT_EQ(
            processor.Process(ServerTick, 0.05f, sessions, commandStore, entityManager, scores, &headOnlyActiveArea),
            WorldTickProcessResult::Processed);
        EXPECT_TRUE(processor.LastPlayerSpawnCandidates().empty());
        EXPECT_EQ(entityManager.Size(), 0u);
    }

    TEST(WorldTickProcessorTests, RetriesLaterSpawnCandidateWhenFirstPlacementIsBlocked)
    {
        constexpr WorldGrowthConfig GrowthConfig{10.0f, 0.25f, 1.0f, 0.01f};
        constexpr WorldTickProcessorConfig Config{
            WorldPhysicsConfig{WorldPhysicsMaxContactsPerTick, 0.0001f, 0.0001f},
            WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f},
            WorldControlMovementConfig{5.0f, 7.5f, std::numbers::pi_v<float>},
            WorldBodyTrailSampleConfig{},
            WorldPlayerBodyConfig{GrowthConfig, 16},
            1,
            4,
        };
        constexpr WorldPlayerSpawnPlannerConfig SpawnConfig{
            Config.arenaBounds,       Config.playerSpawnMaxCandidatesPerTick,
            Config.playerArchetypeId, Config.controlMovement.baseSpeed,
            Config.playerBody,
        };
        WorldResult<WorldPlayerSpawnCandidate> blockedCandidateResult =
            WorldPlayerSpawnPlanner::Plan(SpawnConfig, 100, 8, 0);
        ASSERT_TRUE(blockedCandidateResult.Succeeded());
        WorldPlayerSpawnCandidate blockedCandidate = blockedCandidateResult.TakeValue();
        blockedCandidate.components.playerControl.playerId = 7;

        WorldEntityManager entityManager;
        WorldEntityKey blockerKey;
        EntityHandle blockerHandle;
        ASSERT_TRUE(entityManager.TryCreate(blockedCandidate.components, &blockerKey, &blockerHandle));
        const WorldEntityKey removedEntityKey{999, 1};
        const std::vector<WorldSession> sessions{
            WorldSession{WorldSessionKey{10}, 7, blockerKey},
            WorldSession{WorldSessionKey{11}, 8, removedEntityKey},
        };
        const std::vector<WorldPlayerScore> scores{
            WorldPlayerScore{7, blockerKey, 0},
            WorldPlayerScore{8, removedEntityKey, 0, 0.0f, WorldPlayerLifecycle::SpawnPending},
        };
        WorldMovementCommandStore commandStore;
        WorldTickProcessor processor{Config};

        ASSERT_EQ(processor.Process(100, 0.05f, sessions, commandStore, entityManager, scores),
                  WorldTickProcessResult::Processed);
        ASSERT_EQ(processor.LastPlayerSpawnCandidates().size(), 1u);
        EXPECT_EQ(processor.LastPlayerSpawnCandidates()[0].playerId, 8u);
        EXPECT_GT(processor.LastPlayerSpawnCandidates()[0].ordinal, 0u);
    }
} // namespace psnr::world::tests
