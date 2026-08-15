#include "pch.h"

#include "WorldReadView.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakePlayerComponents()
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{10.0f, 20.0f, 0.5f};
            components.motion = MotionComponent{1.0f, 2.0f, 0.25f, -0.5f};
            components.movementCapability = MovementCapabilityComponent{6.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 100, WorldShapeKind::Circle, 0.75f};
            components.playerControl = PlayerControlComponent{7};
            components.physicsBinding.proxyIds = {PhysicsProxyId{11}, PhysicsProxyId{12}};
            return components;
        }
    } // namespace

    TEST(WorldReadViewTests, ReadsComponentsByWorldEntityKeyWithoutExposingHandle)
    {
        WorldEntityManager manager;
        const WorldEntityComponents expected = MakePlayerComponents();
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents actual;

        ASSERT_TRUE(manager.TryCreate(expected, &entityKey, &handle));
        const WorldReadView readView{manager};

        ASSERT_TRUE(readView.TryReadComponents(entityKey, &actual));
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(readView.EntityCount(), 1u);
    }

    TEST(WorldReadViewTests, ReturnedComponentCopyCannotMutateCanonicalState)
    {
        WorldEntityManager manager;
        const WorldEntityComponents expected = MakePlayerComponents();
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents localCopy;
        WorldEntityComponents canonical;

        ASSERT_TRUE(manager.TryCreate(expected, &entityKey, &handle));
        const WorldReadView readView{manager};
        ASSERT_TRUE(readView.TryReadComponents(entityKey, &localCopy));

        localCopy.transform.positionX = 999.0f;
        localCopy.motion.movementIntentX = -1.0f;

        ASSERT_TRUE(readView.TryReadComponents(entityKey, &canonical));
        EXPECT_EQ(canonical, expected);
    }

    TEST(WorldReadViewTests, RejectsUnknownEntityAndNullOutputWithoutChangingOutput)
    {
        WorldEntityManager manager;
        const WorldEntityComponents unchanged = MakePlayerComponents();
        constexpr WorldEntityKey UnknownKey{99, 1};
        WorldEntityComponents output = unchanged;
        const WorldReadView readView{manager};

        EXPECT_FALSE(readView.TryReadComponents(UnknownKey, &output));
        EXPECT_EQ(output, unchanged);
        EXPECT_FALSE(readView.TryReadComponents(UnknownKey, nullptr));
        EXPECT_EQ(output, unchanged);
    }
} // namespace psnr::world::tests
