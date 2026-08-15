#include "pch.h"

#include "WorldEntityComponentStore.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakePlayerComponents()
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{10.0f, 20.0f, 0.5f};
            components.motion = MotionComponent{1.0f, 2.0f, 0.0f, 1.0f};
            components.movementCapability = MovementCapabilityComponent{6.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 100, WorldShapeKind::Circle, 0.75f};
            components.playerControl = PlayerControlComponent{7};
            components.physicsBinding.proxyIds = {PhysicsProxyId{11}, PhysicsProxyId{12}};
            return components;
        }
    } // namespace

    TEST(WorldEntityComponentStoreTests, CreatesAndReadsCompleteComponentBundle)
    {
        WorldEntityComponentStore store;
        constexpr EntityHandle Handle{0, 1};
        const WorldEntityComponents expected = MakePlayerComponents();
        WorldEntityComponents actual;

        ASSERT_TRUE(store.TryCreate(Handle, expected));
        ASSERT_TRUE(store.TryRead(Handle, &actual));
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(store.Size(), 1u);
    }

    TEST(WorldEntityComponentStoreTests, ReplacesCompleteComponentBundle)
    {
        WorldEntityComponentStore store;
        constexpr EntityHandle Handle{0, 1};
        const WorldEntityComponents initial = MakePlayerComponents();
        WorldEntityComponents replacement = initial;
        WorldEntityComponents actual;
        replacement.transform.positionX = 30.0f;
        replacement.motion.velocityX = 4.0f;

        ASSERT_TRUE(store.TryCreate(Handle, initial));
        ASSERT_TRUE(store.TryReplace(Handle, replacement));
        ASSERT_TRUE(store.TryRead(Handle, &actual));
        EXPECT_EQ(actual, replacement);
        EXPECT_EQ(store.Size(), 1u);
    }

    TEST(WorldEntityComponentStoreTests, RemovalRejectsStaleHandleAndAllowsNewGeneration)
    {
        WorldEntityComponentStore store;
        constexpr EntityHandle OldHandle{0, 1};
        constexpr EntityHandle NewHandle{0, 2};
        const WorldEntityComponents components = MakePlayerComponents();
        WorldEntityComponents output;

        ASSERT_TRUE(store.TryCreate(OldHandle, components));
        ASSERT_TRUE(store.Remove(OldHandle));
        EXPECT_FALSE(store.TryRead(OldHandle, &output));
        EXPECT_FALSE(store.TryReplace(OldHandle, components));
        EXPECT_FALSE(store.Remove(OldHandle));

        ASSERT_TRUE(store.TryCreate(NewHandle, components));
        EXPECT_FALSE(store.TryRead(OldHandle, &output));
        EXPECT_TRUE(store.TryRead(NewHandle, &output));
        EXPECT_EQ(store.Size(), 1u);
    }

    TEST(WorldEntityComponentStoreTests, RejectsInvalidDuplicateSparseAndNullOutputOperations)
    {
        WorldEntityComponentStore store;
        constexpr EntityHandle InvalidHandle{0, 0};
        constexpr EntityHandle FirstHandle{0, 1};
        constexpr EntityHandle SparseHandle{2, 1};
        const WorldEntityComponents components = MakePlayerComponents();

        EXPECT_FALSE(store.TryCreate(InvalidHandle, components));
        EXPECT_FALSE(store.TryCreate(SparseHandle, components));
        ASSERT_TRUE(store.TryCreate(FirstHandle, components));
        EXPECT_FALSE(store.TryCreate(FirstHandle, components));
        EXPECT_FALSE(store.TryRead(FirstHandle, nullptr));
        EXPECT_EQ(store.Size(), 1u);
    }

    TEST(WorldEntityComponentStoreTests, RemovalMovesLastDenseComponentAndUpdatesSparseMapping)
    {
        WorldEntityComponentStore store;
        constexpr EntityHandle FirstHandle{0, 1};
        constexpr EntityHandle RemovedHandle{1, 1};
        constexpr EntityHandle MovedHandle{2, 1};
        constexpr EntityHandle ReusedHandle{1, 2};
        const WorldEntityComponents firstComponents = MakePlayerComponents();
        WorldEntityComponents removedComponents = MakePlayerComponents();
        WorldEntityComponents movedComponents = MakePlayerComponents();
        WorldEntityComponents reusedComponents = MakePlayerComponents();
        WorldEntityComponents output;
        removedComponents.playerControl.playerId = 8;
        movedComponents.playerControl.playerId = 9;
        reusedComponents.playerControl.playerId = 10;

        ASSERT_TRUE(store.TryCreate(FirstHandle, firstComponents));
        ASSERT_TRUE(store.TryCreate(RemovedHandle, removedComponents));
        ASSERT_TRUE(store.TryCreate(MovedHandle, movedComponents));
        ASSERT_TRUE(store.Remove(RemovedHandle));

        EXPECT_FALSE(store.TryRead(RemovedHandle, &output));
        ASSERT_TRUE(store.TryRead(MovedHandle, &output));
        EXPECT_EQ(output, movedComponents);

        ASSERT_TRUE(store.TryCreate(ReusedHandle, reusedComponents));
        ASSERT_TRUE(store.TryRead(ReusedHandle, &output));
        EXPECT_EQ(output, reusedComponents);
        ASSERT_TRUE(store.TryRead(MovedHandle, &output));
        EXPECT_EQ(output, movedComponents);
        EXPECT_EQ(store.Size(), 3u);
    }
} // namespace psnr::world::tests
