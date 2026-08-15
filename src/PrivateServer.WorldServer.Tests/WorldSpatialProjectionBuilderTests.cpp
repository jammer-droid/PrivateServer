#include "pch.h"

#include "WorldSpatialProjectionBuilder.h"

#include <vector>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents CreateReplicableComponents(const WorldEntityKind entityKind,
                                                                       const float positionX, const float positionY,
                                                                       const float radius)
        {
            WorldEntityComponents components;
            components.transform.positionX = positionX;
            components.transform.positionY = positionY;
            components.replicationMetadata = ReplicationMetadataComponent{
                entityKind,
                1,
                WorldShapeKind::Circle,
                radius,
            };
            if (entityKind == WorldEntityKind::Player)
            {
                WorldResult<BodyTrailComponent> bodyTrailResult = CreateBodyTrailComponent(8);
                EXPECT_TRUE(bodyTrailResult.Succeeded());
                components.bodyTrail = bodyTrailResult.TakeValue();
                EXPECT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{positionX, positionY}));
            }
            return components;
        }
    } // namespace

    TEST(WorldSpatialProjectionBuilderTests, BuildsSortedPostCommitSpatialProxies)
    {
        WorldEntityManager entityManager;
        WorldEntityKey firstKey;
        EntityHandle firstHandle;
        ASSERT_TRUE(entityManager.TryCreate(CreateReplicableComponents(WorldEntityKind::Player, 1.0f, 2.0f, 0.5f),
                                            &firstKey, &firstHandle));
        WorldEntityKey secondKey;
        EntityHandle secondHandle;
        ASSERT_TRUE(entityManager.TryCreate(CreateReplicableComponents(WorldEntityKind::Resource, 3.0f, 4.0f, 0.75f),
                                            &secondKey, &secondHandle));
        ASSERT_TRUE(entityManager.Remove(firstHandle));

        WorldEntityKey recycledFirstKey;
        EntityHandle recycledFirstHandle;
        ASSERT_TRUE(entityManager.TryCreate(CreateReplicableComponents(WorldEntityKind::Player, 5.0f, 6.0f, 0.5f),
                                            &recycledFirstKey, &recycledFirstHandle));
        ASSERT_EQ(recycledFirstKey.entityId, firstKey.entityId);
        ASSERT_NE(recycledFirstKey.generation, firstKey.generation);

        WorldEntityComponents committedComponents;
        ASSERT_TRUE(entityManager.TryReadComponents(recycledFirstHandle, &committedComponents));
        committedComponents.transform.positionX = 10.0f;
        committedComponents.transform.positionY = 20.0f;
        ASSERT_TRUE(committedComponents.bodyTrail.TryWrite(0, BodyTrailSample{10.0f, 20.0f}));
        ASSERT_TRUE(entityManager.TryReplaceComponents(recycledFirstHandle, committedComponents));

        WorldResult<std::vector<WorldSpatialProxy>> result = WorldSpatialProjectionBuilder::Build(entityManager);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<WorldSpatialProxy> proxies = result.TakeValue();
        ASSERT_EQ(proxies.size(), 2u);
        EXPECT_EQ(proxies[0].entityKey, recycledFirstKey);
        EXPECT_EQ(proxies[0].entityKind, WorldEntityKind::Player);
        EXPECT_FLOAT_EQ(proxies[0].centerX, 10.0f);
        EXPECT_FLOAT_EQ(proxies[0].centerY, 20.0f);
        EXPECT_FLOAT_EQ(proxies[0].circleRadius, 0.5f);
        EXPECT_EQ(proxies[1].entityKey, secondKey);
        EXPECT_EQ(proxies[1].entityKind, WorldEntityKind::Resource);
    }

    TEST(WorldSpatialProjectionBuilderTests, BoundsPlayerTrailFromHeadAndKeepsResourceCircle)
    {
        WorldEntityManager entityManager;
        WorldEntityComponents player = CreateReplicableComponents(WorldEntityKind::Player, 0.0f, 0.0f, 1.0f);
        ASSERT_TRUE(player.bodyTrail.TryPushBack(BodyTrailSample{3.0f, 4.0f}));
        ASSERT_TRUE(player.bodyTrail.TryPushBack(BodyTrailSample{-6.0f, 8.0f}));
        WorldEntityKey playerKey;
        EntityHandle playerHandle;
        ASSERT_TRUE(entityManager.TryCreate(player, &playerKey, &playerHandle));

        WorldEntityKey resourceKey;
        EntityHandle resourceHandle;
        ASSERT_TRUE(entityManager.TryCreate(CreateReplicableComponents(WorldEntityKind::Resource, 2.0f, 0.0f, 0.25f),
                                            &resourceKey, &resourceHandle));

        WorldResult<std::vector<WorldSpatialProxy>> result = WorldSpatialProjectionBuilder::Build(entityManager);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<WorldSpatialProxy> proxies = result.TakeValue();
        ASSERT_EQ(proxies.size(), 2u);
        EXPECT_EQ(proxies[0].entityKey, playerKey);
        EXPECT_FLOAT_EQ(proxies[0].circleRadius, 11.0f);
        EXPECT_EQ(proxies[1].entityKey, resourceKey);
        EXPECT_FLOAT_EQ(proxies[1].circleRadius, 0.25f);
    }

    TEST(WorldSpatialProjectionBuilderTests, RejectsInvalidEntity)
    {
        WorldEntityManager entityManager;
        WorldEntityKey invalidKey;
        EntityHandle invalidHandle;
        WorldEntityComponents invalidComponents = CreateReplicableComponents(WorldEntityKind::Player, 1.0f, 2.0f, 0.5f);
        invalidComponents.replicationMetadata.primaryShapeKind = WorldShapeKind::Invalid;
        ASSERT_TRUE(entityManager.TryCreate(invalidComponents, &invalidKey, &invalidHandle));

        const WorldResult<std::vector<WorldSpatialProxy>> result = WorldSpatialProjectionBuilder::Build(entityManager);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidState);
    }
} // namespace psnr::world
