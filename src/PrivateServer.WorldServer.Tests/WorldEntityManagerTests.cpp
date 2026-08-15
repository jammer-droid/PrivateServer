#include "pch.h"

#include "WorldEntityManager.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakeComponents(const std::uint32_t playerId)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{10.0f, 20.0f, 0.5f};
            components.motion = MotionComponent{1.0f, 2.0f, 0.0f, 1.0f};
            components.movementCapability = MovementCapabilityComponent{6.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 100, WorldShapeKind::Circle, 0.75f};
            components.playerControl = PlayerControlComponent{playerId};
            components.physicsBinding.proxyIds = {PhysicsProxyId{11}, PhysicsProxyId{12}};
            return components;
        }
    } // namespace

    TEST(WorldEntityManagerTests, CreatesEntityAndResolvesIdentityAndComponents)
    {
        WorldEntityManager manager;
        const WorldEntityComponents expectedComponents = MakeComponents(7);
        WorldEntityKey createdKey;
        EntityHandle createdHandle;
        WorldEntityKey foundKey;
        EntityHandle foundHandle;
        WorldEntityComponents actualComponents;

        ASSERT_TRUE(manager.TryCreate(expectedComponents, &createdKey, &createdHandle));
        EXPECT_EQ(createdKey, (WorldEntityKey{1, 1}));
        EXPECT_EQ(createdHandle, (EntityHandle{0, 1}));
        ASSERT_TRUE(manager.TryFindHandle(createdKey, &foundHandle));
        EXPECT_EQ(foundHandle, createdHandle);
        ASSERT_TRUE(manager.TryFindKey(createdHandle, &foundKey));
        EXPECT_EQ(foundKey, createdKey);
        ASSERT_TRUE(manager.TryReadComponents(createdHandle, &actualComponents));
        EXPECT_EQ(actualComponents, expectedComponents);
        EXPECT_EQ(manager.Size(), 1u);
    }

    TEST(WorldEntityManagerTests, ReplacesCompleteComponentBundle)
    {
        WorldEntityManager manager;
        const WorldEntityComponents initial = MakeComponents(7);
        WorldEntityComponents replacement = MakeComponents(8);
        WorldEntityKey key;
        EntityHandle handle;
        WorldEntityComponents actual;
        replacement.transform.positionX = 30.0f;

        ASSERT_TRUE(manager.TryCreate(initial, &key, &handle));
        ASSERT_TRUE(manager.TryReplaceComponents(handle, replacement));
        ASSERT_TRUE(manager.TryReadComponents(handle, &actual));
        EXPECT_EQ(actual, replacement);
    }

    TEST(WorldEntityManagerTests, RemovalInvalidatesOldIdentityAndReusesBothGenerations)
    {
        WorldEntityManager manager;
        const WorldEntityComponents removedComponents = MakeComponents(7);
        const WorldEntityComponents replacementComponents = MakeComponents(8);
        WorldEntityKey removedKey;
        EntityHandle removedHandle;
        WorldEntityKey replacementKey;
        EntityHandle replacementHandle;
        WorldEntityKey foundKey;
        EntityHandle foundHandle;
        WorldEntityComponents actual;

        ASSERT_TRUE(manager.TryCreate(removedComponents, &removedKey, &removedHandle));
        ASSERT_TRUE(manager.Remove(removedHandle));
        EXPECT_FALSE(manager.TryFindHandle(removedKey, &foundHandle));
        EXPECT_FALSE(manager.TryFindKey(removedHandle, &foundKey));
        EXPECT_FALSE(manager.TryReadComponents(removedHandle, &actual));
        EXPECT_FALSE(manager.TryReplaceComponents(removedHandle, removedComponents));
        EXPECT_FALSE(manager.Remove(removedHandle));

        ASSERT_TRUE(manager.TryCreate(replacementComponents, &replacementKey, &replacementHandle));
        EXPECT_EQ(replacementKey.entityId, removedKey.entityId);
        EXPECT_EQ(replacementKey.generation, removedKey.generation + 1);
        EXPECT_EQ(replacementHandle.slotIndex, removedHandle.slotIndex);
        EXPECT_EQ(replacementHandle.slotGeneration, removedHandle.slotGeneration + 1);
        ASSERT_TRUE(manager.TryReadComponents(replacementHandle, &actual));
        EXPECT_EQ(actual, replacementComponents);
        EXPECT_EQ(manager.Size(), 1u);
    }

    TEST(WorldEntityManagerTests, RejectedCreatePreservesOutputsAndDoesNotConsumeIdentity)
    {
        WorldEntityManager manager;
        constexpr WorldEntityKey UnchangedKey{99, 9};
        constexpr EntityHandle UnchangedHandle{88, 8};
        WorldEntityKey key = UnchangedKey;
        EntityHandle handle = UnchangedHandle;
        const WorldEntityComponents components = MakeComponents(7);

        EXPECT_FALSE(manager.TryCreate(components, nullptr, &handle));
        EXPECT_EQ(handle, UnchangedHandle);
        EXPECT_FALSE(manager.TryCreate(components, &key, nullptr));
        EXPECT_EQ(key, UnchangedKey);
        EXPECT_EQ(manager.Size(), 0u);

        ASSERT_TRUE(manager.TryCreate(components, &key, &handle));
        EXPECT_EQ(key, (WorldEntityKey{1, 1}));
        EXPECT_EQ(handle, (EntityHandle{0, 1}));
    }

    TEST(WorldEntityManagerTests, ReadViewReturnsCanonicalBundleWithoutCopy)
    {
        WorldEntityManager manager;
        const WorldEntityComponents components = MakeComponents(7);
        WorldEntityKey key;
        EntityHandle handle;
        const WorldEntityComponents* firstView = nullptr;
        const WorldEntityComponents* secondView = nullptr;

        ASSERT_TRUE(manager.TryCreate(components, &key, &handle));
        ASSERT_TRUE(manager.TryReadComponentsView(handle, &firstView));
        ASSERT_TRUE(manager.TryReadComponentsView(handle, &secondView));
        ASSERT_NE(firstView, nullptr);
        EXPECT_EQ(firstView, secondView);
        EXPECT_EQ(*firstView, components);
    }
} // namespace psnr::world::tests
