#include "pch.h"

#include "WorldMovementPhaseCommitter.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakeComponents(const float positionX, const std::uint32_t playerId)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{positionX, 20.0f, 0.5f};
            components.motion = MotionComponent{1.0f, 2.0f, 0.25f, -0.5f};
            components.movementCapability = MovementCapabilityComponent{6.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 100, WorldShapeKind::Circle, 0.75f};
            components.playerControl = PlayerControlComponent{playerId};
            components.physicsBinding.proxyIds = {PhysicsProxyId{11}, PhysicsProxyId{12}};
            return components;
        }

        [[nodiscard]] WorldMovementEntityUpdate MakeUpdate(const WorldEntityKey entityKey, const float positionX)
        {
            return WorldMovementEntityUpdate{
                entityKey,
                TransformComponent{positionX, 40.0f, 1.0f},
                MotionComponent{3.0f, 4.0f, 0.5f, 0.75f},
            };
        }
    } // namespace

    TEST(WorldMovementPhaseCommitterTests, ResultOrdersUpdatesByWorldEntityKey)
    {
        constexpr WorldEntityKey FirstKey{1, 2};
        constexpr WorldEntityKey SecondKey{2, 1};
        const WorldMovementPhaseResult result{{MakeUpdate(SecondKey, 20.0f), MakeUpdate(FirstKey, 10.0f)}};
        const std::span<const WorldMovementEntityUpdate> updates = result.Updates();

        ASSERT_EQ(updates.size(), 2u);
        EXPECT_EQ(updates[0].entityKey, FirstKey);
        EXPECT_EQ(updates[1].entityKey, SecondKey);
    }

    TEST(WorldMovementPhaseCommitterTests, CommitsMovementStateAndPreservesOtherComponents)
    {
        WorldEntityManager manager;
        const WorldEntityComponents original = MakeComponents(10.0f, 7);
        WorldEntityKey entityKey;
        EntityHandle handle;
        WorldEntityComponents actual;

        ASSERT_TRUE(manager.TryCreate(original, &entityKey, &handle));
        const WorldMovementEntityUpdate update = MakeUpdate(entityKey, 30.0f);
        const WorldMovementPhaseResult result{{update}};

        WorldMovementPhaseCommitter::Commit(result, manager);
        ASSERT_TRUE(manager.TryReadComponents(handle, &actual));
        EXPECT_EQ(actual.transform, update.transform);
        EXPECT_EQ(actual.motion, update.motion);
        EXPECT_EQ(actual.movementCapability, original.movementCapability);
        EXPECT_EQ(actual.replicationMetadata, original.replicationMetadata);
        EXPECT_EQ(actual.playerControl, original.playerControl);
        EXPECT_EQ(actual.physicsBinding, original.physicsBinding);
    }

} // namespace psnr::world::tests
