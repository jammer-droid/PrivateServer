#include "pch.h"

#include "WorldPhysicsScene.h"
#include "WorldPhysicsStepResult.h"

#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        constexpr WorldPhysicsConfig ValidConfig{
            WorldPhysicsMaxContactsPerTick,
            0.001f,
            0.0001f,
        };

        constexpr PhysicsProxy ValidProxy{
            WorldEntityKey{7, 2},
            EntityHandle{3, 4},
            PhysicsFixtureId{1},
            0.0f,
            0.0f,
            PhysicsCircleShape{0.5f},
            1,
            0,
            PhysicsProxyBehavior::Solid,
        };

        constexpr WorldPhysicsArenaBounds WideArena{
            -100.0f,
            -100.0f,
            100.0f,
            100.0f,
        };

        [[nodiscard]] std::unique_ptr<WorldPhysicsScene> CreateTestScene()
        {
            WorldResult<std::unique_ptr<WorldPhysicsScene>> result = WorldPhysicsScene::Create(ValidConfig);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] PhysicsProxy CreateSolidProxy(const WorldEntityKey ownerKey, const EntityHandle ownerHandle,
                                                    const std::uint32_t fixtureId, const float radius)
        {
            return PhysicsProxy{
                ownerKey, ownerHandle, PhysicsFixtureId{fixtureId}, 0.0f, 0.0f, PhysicsCircleShape{radius},
                1,        1,           PhysicsProxyBehavior::Solid,
            };
        }

        [[nodiscard]] PhysicsProxy CreateTriggerProxy(const WorldEntityKey ownerKey, const EntityHandle ownerHandle,
                                                      const std::uint32_t fixtureId, const float radius)
        {
            PhysicsProxy proxy = CreateSolidProxy(ownerKey, ownerHandle, fixtureId, radius);
            proxy.behavior = PhysicsProxyBehavior::Trigger;
            return proxy;
        }

        [[nodiscard]] WorldResult<void> ComputeAndStore(
            const WorldPhysicsScene& scene, const std::span<const WorldPhysicsMovementInput> movementInputs,
            const std::span<const WorldPhysicsProxyProjection> staticSolidProxies,
            const std::span<const WorldPhysicsProxyProjection> triggerProxies,
            const WorldPhysicsArenaBounds& arenaBounds, const float fixedDeltaSeconds,
            WorldPhysicsStepResult* const outResult)
        {
            WorldResult<WorldPhysicsStepResult> result =
                scene.Compute(movementInputs, staticSolidProxies, triggerProxies, arenaBounds, fixedDeltaSeconds);
            if (result.Failed())
            {
                return WorldResult<void>::Failure(result.Error());
            }
            *outResult = result.TakeValue();
            return WorldResult<void>::Success();
        }

        [[nodiscard]] WorldResult<void> QueryAndStore(WorldPhysicsScene& scene,
                                                      const std::span<const WorldCollisionProxy> proxies,
                                                      std::vector<WorldCollisionContact>* const outContacts)
        {
            WorldResult<std::vector<WorldCollisionContact>> result = scene.QueryPlayerCollisions(proxies);
            if (result.Failed())
            {
                return WorldResult<void>::Failure(result.Error());
            }
            *outContacts = result.TakeValue();
            return WorldResult<void>::Success();
        }
    } // namespace

    TEST(WorldPhysicsSceneTests, CreatesSceneFromExplicitValidConfig)
    {
        WorldResult<std::unique_ptr<WorldPhysicsScene>> result = WorldPhysicsScene::Create(ValidConfig);
        ASSERT_TRUE(result.Succeeded());
        std::unique_ptr<WorldPhysicsScene> scene = result.TakeValue();
        ASSERT_NE(scene, nullptr);
        EXPECT_EQ(scene->Config(), ValidConfig);
    }

    TEST(WorldPhysicsSceneTests, RejectsInvalidConfig)
    {
        WorldPhysicsConfig invalidContactCount = ValidConfig;
        invalidContactCount.maxContactsPerTick = WorldPhysicsMaxContactsPerTick + 1;
        WorldPhysicsConfig invalidEpsilon = ValidConfig;
        invalidEpsilon.collisionEpsilon = 0.0f;
        WorldPhysicsConfig invalidTolerance = ValidConfig;
        invalidTolerance.contactTolerance = std::numeric_limits<float>::quiet_NaN();

        const WorldResult<std::unique_ptr<WorldPhysicsScene>> invalidContactResult =
            WorldPhysicsScene::Create(invalidContactCount);
        const WorldResult<std::unique_ptr<WorldPhysicsScene>> invalidEpsilonResult =
            WorldPhysicsScene::Create(invalidEpsilon);
        const WorldResult<std::unique_ptr<WorldPhysicsScene>> invalidToleranceResult =
            WorldPhysicsScene::Create(invalidTolerance);

        ASSERT_TRUE(invalidContactResult.Failed());
        ASSERT_TRUE(invalidEpsilonResult.Failed());
        ASSERT_TRUE(invalidToleranceResult.Failed());
        EXPECT_EQ(invalidContactResult.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(invalidEpsilonResult.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(invalidToleranceResult.Error(), WorldErrorCode::InvalidConfig);
    }

    TEST(WorldPhysicsSceneTests, ValidatesLibraryIndependentProxyContract)
    {
        EXPECT_TRUE(IsValid(ValidProxy));

        PhysicsProxy invalidOwner = ValidProxy;
        invalidOwner.ownerKey = WorldEntityKey{};
        PhysicsProxy invalidHandle = ValidProxy;
        invalidHandle.ownerHandle = EntityHandle{};
        PhysicsProxy invalidFixture = ValidProxy;
        invalidFixture.fixtureId = PhysicsFixtureId{};
        PhysicsProxy invalidOffset = ValidProxy;
        invalidOffset.localOffsetX = std::numeric_limits<float>::infinity();
        PhysicsProxy invalidRadius = ValidProxy;
        invalidRadius.shape.radius = 0.0f;
        PhysicsProxy invalidLayer = ValidProxy;
        invalidLayer.layer = 0;
        PhysicsProxy invalidBehavior = ValidProxy;
        invalidBehavior.behavior = PhysicsProxyBehavior::Invalid;

        EXPECT_FALSE(IsValid(invalidOwner));
        EXPECT_FALSE(IsValid(invalidHandle));
        EXPECT_FALSE(IsValid(invalidFixture));
        EXPECT_FALSE(IsValid(invalidOffset));
        EXPECT_FALSE(IsValid(invalidRadius));
        EXPECT_FALSE(IsValid(invalidLayer));
        EXPECT_FALSE(IsValid(invalidBehavior));
    }

    TEST(WorldPhysicsSceneTests, OwnsTypedStepResultsWithoutWorldStateAccess)
    {
        const WorldResolvedMotion resolvedMotion{
            WorldEntityKey{7, 2}, EntityHandle{3, 4}, 10.0f, 20.0f, 1.0f, 2.0f, false,
        };
        const WorldPhysicsContact contact{
            WorldEntityKey{7, 2},
            0,
            PhysicsColliderKey{
                PhysicsColliderKind::ArenaBoundary,
                PhysicsArenaBoundary::Left,
                PhysicsProxyKey{},
            },
            0.25f,
            1.0f,
            0.0f,
        };
        const WorldTriggerOverlap overlap{
            PhysicsProxyKey{WorldEntityKey{7, 2}, PhysicsFixtureId{2}},
            PhysicsProxyKey{WorldEntityKey{9, 1}, PhysicsFixtureId{1}},
        };

        const WorldPhysicsStepResult result{
            std::vector<WorldResolvedMotion>{resolvedMotion},
            std::vector<WorldPhysicsContact>{contact},
            std::vector<WorldTriggerOverlap>{overlap},
        };

        ASSERT_EQ(result.ResolvedMotions().size(), 1);
        ASSERT_EQ(result.Contacts().size(), 1);
        ASSERT_EQ(result.TriggerOverlaps().size(), 1);
        EXPECT_EQ(result.ResolvedMotions().front(), resolvedMotion);
        EXPECT_EQ(result.Contacts().front(), contact);
        EXPECT_EQ(result.TriggerOverlaps().front(), overlap);
    }

    TEST(WorldPhysicsSceneTests, ResolvesUnblockedNormalizedMovement)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            3.0f,
            4.0f,
            10.0f,
        };
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, std::span<const WorldPhysicsMovementInput>{&movement, 1}, {}, {}, WideArena,
                                    1.0f, &result)
                        .Succeeded());
        ASSERT_EQ(result.ResolvedMotions().size(), 1);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionX, 6.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionY, 8.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].velocityX, 6.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].velocityY, 8.0f, 0.0001f);
        EXPECT_FALSE(result.ResolvedMotions()[0].contactCapReached);
        EXPECT_TRUE(result.Contacts().empty());
    }

    TEST(WorldPhysicsSceneTests, SweepsToArenaContactAndSlidesAlongBoundary)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            0.8f,
            0.6f,
            10.0f,
        };
        constexpr WorldPhysicsArenaBounds Arena{-10.0f, -10.0f, 5.0f, 10.0f};
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, std::span<const WorldPhysicsMovementInput>{&movement, 1}, {}, {}, Arena,
                                    1.0f, &result)
                        .Succeeded());
        ASSERT_EQ(result.ResolvedMotions().size(), 1);
        ASSERT_EQ(result.Contacts().size(), 1);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionX, 4.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionY, 6.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].velocityX, 4.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].velocityY, 6.0f, 0.0001f);
        EXPECT_EQ(result.Contacts()[0].collider.kind, PhysicsColliderKind::ArenaBoundary);
        EXPECT_EQ(result.Contacts()[0].collider.arenaBoundary, PhysicsArenaBoundary::Right);
        EXPECT_NEAR(result.Contacts()[0].timeOfImpact, 0.5f, 0.0001f);
        EXPECT_NEAR(result.Contacts()[0].normalX, -1.0f, 0.0001f);
        EXPECT_NEAR(result.Contacts()[0].normalY, 0.0f, 0.0001f);
    }

    TEST(WorldPhysicsSceneTests, SweepsAgainstStaticCircleWithoutTunneling)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const PhysicsProxy staticProxy = CreateSolidProxy(WorldEntityKey{2, 1}, EntityHandle{1, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            1.0f,
            0.0f,
            10.0f,
        };
        const WorldPhysicsProxyProjection obstacle{staticProxy, 6.0f, 0.0f};
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, std::span<const WorldPhysicsMovementInput>{&movement, 1},
                                    std::span<const WorldPhysicsProxyProjection>{&obstacle, 1}, {}, WideArena, 1.0f,
                                    &result)
                        .Succeeded());
        ASSERT_EQ(result.ResolvedMotions().size(), 1);
        ASSERT_EQ(result.Contacts().size(), 1);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionX, 4.0f, 0.0001f);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionY, 0.0f, 0.0001f);
        EXPECT_EQ(result.Contacts()[0].collider.kind, PhysicsColliderKind::EntityProxy);
        EXPECT_EQ(result.Contacts()[0].collider.entityProxy,
                  (PhysicsProxyKey{staticProxy.ownerKey, staticProxy.fixtureId}));
        EXPECT_NEAR(result.Contacts()[0].timeOfImpact, 0.4f, 0.0001f);
        EXPECT_NEAR(result.Contacts()[0].normalX, -1.0f, 0.0001f);
    }

    TEST(WorldPhysicsSceneTests, RejectsInitialSolidPenetration)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const PhysicsProxy staticProxy = CreateSolidProxy(WorldEntityKey{2, 1}, EntityHandle{1, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            1.0f,
            0.0f,
            1.0f,
        };
        const WorldPhysicsProxyProjection obstacle{staticProxy, 1.5f, 0.0f};
        const WorldResult<WorldPhysicsStepResult> result =
            scene->Compute(std::span<const WorldPhysicsMovementInput>{&movement, 1},
                           std::span<const WorldPhysicsProxyProjection>{&obstacle, 1}, {}, WideArena, 1.0f);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InitialPenetration);
    }

    TEST(WorldPhysicsSceneTests, SelectsStableColliderKeyForTiedContacts)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{5, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const PhysicsProxy largerKeyProxy = CreateSolidProxy(WorldEntityKey{3, 1}, EntityHandle{1, 1}, 1, 1.0f);
        const PhysicsProxy smallerKeyProxy = CreateSolidProxy(WorldEntityKey{2, 1}, EntityHandle{2, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            1.0f,
            0.0f,
            10.0f,
        };
        const std::vector<WorldPhysicsProxyProjection> obstacles{
            WorldPhysicsProxyProjection{largerKeyProxy, 6.0f, 0.0f},
            WorldPhysicsProxyProjection{smallerKeyProxy, 6.00005f, 0.0f},
        };
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, std::span<const WorldPhysicsMovementInput>{&movement, 1}, obstacles, {},
                                    WideArena, 1.0f, &result)
                        .Succeeded());
        ASSERT_EQ(result.Contacts().size(), 1);
        EXPECT_EQ(result.Contacts()[0].collider.entityProxy,
                  (PhysicsProxyKey{smallerKeyProxy.ownerKey, smallerKeyProxy.fixtureId}));
    }

    TEST(WorldPhysicsSceneTests, SortsContactsByMovingOwnerAndOrdinal)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy largerOwnerProxy = CreateSolidProxy(WorldEntityKey{2, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const PhysicsProxy smallerOwnerProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{1, 1}, 1, 1.0f);
        const std::vector<WorldPhysicsMovementInput> movements{
            WorldPhysicsMovementInput{
                WorldPhysicsProxyProjection{largerOwnerProxy, 0.0f, 0.0f},
                1.0f,
                0.0f,
                10.0f,
            },
            WorldPhysicsMovementInput{
                WorldPhysicsProxyProjection{smallerOwnerProxy, 0.0f, 0.0f},
                1.0f,
                0.0f,
                10.0f,
            },
        };
        constexpr WorldPhysicsArenaBounds Arena{-10.0f, -10.0f, 5.0f, 10.0f};
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, movements, {}, {}, Arena, 1.0f, &result).Succeeded());
        ASSERT_EQ(result.Contacts().size(), 2);
        EXPECT_EQ(result.Contacts()[0].movingOwnerKey, smallerOwnerProxy.ownerKey);
        EXPECT_EQ(result.Contacts()[1].movingOwnerKey, largerOwnerProxy.ownerKey);
        EXPECT_EQ(result.Contacts()[0].contactOrdinal, 0u);
        EXPECT_EQ(result.Contacts()[1].contactOrdinal, 0u);
    }

    TEST(WorldPhysicsSceneTests, ComputesSortedUniqueTriggerOverlapsAtFinalPosition)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{5, 1}, EntityHandle{0, 1}, 2, 1.0f);
        const PhysicsProxy passedTrigger = CreateTriggerProxy(WorldEntityKey{4, 1}, EntityHandle{1, 1}, 1, 1.0f);
        const PhysicsProxy largerKeyTrigger = CreateTriggerProxy(WorldEntityKey{3, 1}, EntityHandle{2, 1}, 1, 1.0f);
        const PhysicsProxy smallerKeyTrigger = CreateTriggerProxy(WorldEntityKey{2, 1}, EntityHandle{3, 1}, 3, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            1.0f,
            0.0f,
            10.0f,
        };
        const std::vector<WorldPhysicsProxyProjection> triggers{
            WorldPhysicsProxyProjection{largerKeyTrigger, 10.0f, 0.0f},
            WorldPhysicsProxyProjection{passedTrigger, 5.0f, 0.0f},
            WorldPhysicsProxyProjection{smallerKeyTrigger, 10.0f, 0.0f},
            WorldPhysicsProxyProjection{largerKeyTrigger, 10.0f, 0.0f},
        };
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, std::span<const WorldPhysicsMovementInput>{&movement, 1}, {}, triggers,
                                    WideArena, 1.0f, &result)
                        .Succeeded());
        ASSERT_EQ(result.ResolvedMotions().size(), 1);
        EXPECT_NEAR(result.ResolvedMotions()[0].positionX, 10.0f, 0.0001f);
        ASSERT_EQ(result.TriggerOverlaps().size(), 2);
        EXPECT_EQ(result.TriggerOverlaps()[0],
                  (WorldTriggerOverlap{
                      PhysicsProxyKey{smallerKeyTrigger.ownerKey, smallerKeyTrigger.fixtureId},
                      PhysicsProxyKey{movingProxy.ownerKey, movingProxy.fixtureId},
                  }));
        EXPECT_EQ(result.TriggerOverlaps()[1],
                  (WorldTriggerOverlap{
                      PhysicsProxyKey{largerKeyTrigger.ownerKey, largerKeyTrigger.fixtureId},
                      PhysicsProxyKey{movingProxy.ownerKey, movingProxy.fixtureId},
                  }));
    }

    TEST(WorldPhysicsSceneTests, ReportsRemainingMotionAfterFourContactCap)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            1.0f,
            0.0f,
            50.0f,
        };
        const std::vector<WorldPhysicsProxyProjection> obstacles{
            WorldPhysicsProxyProjection{CreateSolidProxy(WorldEntityKey{2, 1}, EntityHandle{1, 1}, 1, 1.0f), 4.0f,
                                        1.0f},
            WorldPhysicsProxyProjection{CreateSolidProxy(WorldEntityKey{3, 1}, EntityHandle{2, 1}, 1, 1.0f), 5.1339746f,
                                        -2.9641016f},
            WorldPhysicsProxyProjection{CreateSolidProxy(WorldEntityKey{4, 1}, EntityHandle{3, 1}, 1, 1.0f), 2.2679492f,
                                        -5.9282032f},
            WorldPhysicsProxyProjection{CreateSolidProxy(WorldEntityKey{5, 1}, EntityHandle{4, 1}, 1, 1.0f),
                                        -1.7320508f, -4.9282032f},
        };
        WorldPhysicsStepResult result;

        ASSERT_TRUE(ComputeAndStore(*scene, std::span<const WorldPhysicsMovementInput>{&movement, 1}, obstacles, {},
                                    WideArena, 1.0f, &result)
                        .Succeeded());
        ASSERT_EQ(result.ResolvedMotions().size(), 1u);
        EXPECT_TRUE(result.ResolvedMotions()[0].contactCapReached);
        EXPECT_EQ(result.Contacts().size(), WorldPhysicsMaxContactsPerTick);
    }

    TEST(WorldPhysicsSceneTests, RejectsSolidProxyInTriggerInput)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const PhysicsProxy movingProxy = CreateSolidProxy(WorldEntityKey{1, 1}, EntityHandle{0, 1}, 1, 1.0f);
        const PhysicsProxy solidProxy = CreateSolidProxy(WorldEntityKey{2, 1}, EntityHandle{1, 1}, 1, 1.0f);
        const WorldPhysicsMovementInput movement{
            WorldPhysicsProxyProjection{movingProxy, 0.0f, 0.0f},
            0.0f,
            0.0f,
            1.0f,
        };
        const WorldPhysicsProxyProjection invalidTrigger{solidProxy, 5.0f, 0.0f};

        const WorldResult<WorldPhysicsStepResult> computeResult =
            scene->Compute(std::span<const WorldPhysicsMovementInput>{&movement, 1}, {},
                           std::span<const WorldPhysicsProxyProjection>{&invalidTrigger, 1}, WideArena, 1.0f);
        ASSERT_TRUE(computeResult.Failed());
        EXPECT_EQ(computeResult.Error(), WorldErrorCode::InvalidInput);
    }

    TEST(WorldPhysicsSceneTests, QueriesStableUniquePlayerContactsThroughBox2dAdapter)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const std::vector<WorldCollisionProxy> proxies{
            WorldCollisionProxy{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerBody, 0, -2.0f, 0.0f, -1.0f, 0.0f,
                                0.5f},
            WorldCollisionProxy{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0, 1.5f, 0.0f, 1.5f, 0.0f,
                                1.0f},
            WorldCollisionProxy{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                1.0f},
        };
        std::vector<WorldCollisionContact> contacts;

        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());
        ASSERT_EQ(contacts.size(), 2u);
        EXPECT_EQ(contacts[0], (WorldCollisionContact{
                                   WorldCollisionProxyKey{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0},
                                   WorldCollisionProxyKey{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0},
                               }));
        EXPECT_EQ(contacts[1], (WorldCollisionContact{
                                   WorldCollisionProxyKey{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0},
                                   WorldCollisionProxyKey{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerBody, 0},
                               }));
    }

    TEST(WorldPhysicsSceneTests, RejectsSelfAndBroadPhaseOnlyPlayerPairs)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const std::vector<WorldCollisionProxy> proxies{
            WorldCollisionProxy{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                1.0f},
            WorldCollisionProxy{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerBody, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                1.0f},
            WorldCollisionProxy{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0, 1.9f, 1.9f, 1.9f, 1.9f,
                                1.0f},
        };
        std::vector<WorldCollisionContact> contacts{
            WorldCollisionContact{
                WorldCollisionProxyKey{WorldEntityKey{9, 1}, WorldCollisionProxyRole::PlayerHead, 0},
                WorldCollisionProxyKey{WorldEntityKey{10, 1}, WorldCollisionProxyRole::PlayerHead, 0},
            },
        };

        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());
        EXPECT_TRUE(contacts.empty());
    }

    TEST(WorldPhysicsSceneTests, SynchronizesPersistentPlayerTreeAcrossTicks)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        std::vector<WorldCollisionProxy> proxies{
            WorldCollisionProxy{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                1.0f},
            WorldCollisionProxy{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0, 1.5f, 0.0f, 1.5f, 0.0f,
                                1.0f},
        };
        std::vector<WorldCollisionContact> contacts;

        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());
        ASSERT_EQ(contacts.size(), 1u);

        proxies[1] = WorldCollisionProxy{
            WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0, 5.0f, 0.0f, 5.0f, 0.0f, 1.0f};
        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());
        EXPECT_TRUE(contacts.empty());

        proxies.resize(1);
        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());
        EXPECT_TRUE(contacts.empty());

        proxies.push_back(WorldCollisionProxy{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0, 1.5f, 0.0f,
                                              1.5f, 0.0f, 1.0f});
        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());
        ASSERT_EQ(contacts.size(), 1u);

        ASSERT_TRUE(QueryAndStore(*scene, {}, &contacts).Succeeded());
        EXPECT_TRUE(contacts.empty());
    }

    TEST(WorldPhysicsSceneTests, ReservesOnlyVacantPlayerSpawnPlacements)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const std::vector<WorldCollisionProxy> proxies{
            WorldCollisionProxy{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                1.0f},
        };
        std::vector<WorldCollisionContact> contacts;
        ASSERT_TRUE(QueryAndStore(*scene, proxies, &contacts).Succeeded());

        EXPECT_EQ(scene->TryReservePlayerSpawnPlacement(WorldPlayerSpawnBounds{-0.5f, -0.5f, 0.5f, 0.5f}),
                  WorldPlayerSpawnReservationResult::Blocked);
        EXPECT_EQ(scene->TryReservePlayerSpawnPlacement(WorldPlayerSpawnBounds{4.0f, 4.0f, 6.0f, 6.0f}),
                  WorldPlayerSpawnReservationResult::Reserved);
        EXPECT_EQ(scene->TryReservePlayerSpawnPlacement(WorldPlayerSpawnBounds{5.0f, 5.0f, 7.0f, 7.0f}),
                  WorldPlayerSpawnReservationResult::Blocked);
        EXPECT_EQ(scene->TryReservePlayerSpawnPlacement(WorldPlayerSpawnBounds{8.0f, 8.0f, 9.0f, 9.0f}),
                  WorldPlayerSpawnReservationResult::Reserved);
    }

    TEST(WorldPhysicsSceneTests, ClearsPlayerSpawnReservationsDuringNextTreeSynchronization)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        std::vector<WorldCollisionContact> contacts;
        ASSERT_TRUE(QueryAndStore(*scene, {}, &contacts).Succeeded());
        const WorldPlayerSpawnBounds bounds{4.0f, 4.0f, 6.0f, 6.0f};

        ASSERT_EQ(scene->TryReservePlayerSpawnPlacement(bounds), WorldPlayerSpawnReservationResult::Reserved);
        ASSERT_EQ(scene->TryReservePlayerSpawnPlacement(bounds), WorldPlayerSpawnReservationResult::Blocked);
        ASSERT_TRUE(QueryAndStore(*scene, {}, &contacts).Succeeded());

        EXPECT_EQ(scene->TryReservePlayerSpawnPlacement(bounds), WorldPlayerSpawnReservationResult::Reserved);
        EXPECT_EQ(scene->TryReservePlayerSpawnPlacement(WorldPlayerSpawnBounds{1.0f, 1.0f, -1.0f, -1.0f}),
                  WorldPlayerSpawnReservationResult::InvalidInput);
    }

    TEST(WorldPhysicsSceneTests, RejectsInvalidPlayerProxies)
    {
        const std::unique_ptr<WorldPhysicsScene> scene = CreateTestScene();
        ASSERT_NE(scene, nullptr);
        const WorldCollisionProxy invalidProxy{
            WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        const WorldResult<std::vector<WorldCollisionContact>> invalidResult =
            scene->QueryPlayerCollisions(std::span<const WorldCollisionProxy>{&invalidProxy, 1});
        ASSERT_TRUE(invalidResult.Failed());
        EXPECT_EQ(invalidResult.Error(), WorldErrorCode::InvalidInput);

        const WorldCollisionProxy duplicateProxy{
            WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        const std::vector<WorldCollisionProxy> duplicateProxies{duplicateProxy, duplicateProxy};
        const WorldResult<std::vector<WorldCollisionContact>> duplicateResult =
            scene->QueryPlayerCollisions(duplicateProxies);
        ASSERT_TRUE(duplicateResult.Failed());
        EXPECT_EQ(duplicateResult.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world
