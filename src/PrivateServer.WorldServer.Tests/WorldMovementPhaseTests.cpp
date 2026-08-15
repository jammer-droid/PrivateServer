#include "pch.h"

#include "WorldMovementPhase.h"

#include <array>
#include <cstdint>
#include <span>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakePhaseComponents(const std::uint32_t playerId, const float positionX)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{positionX, 20.0f, 0.5f};
            components.motion = MotionComponent{};
            components.movementCapability = MovementCapabilityComponent{10.0f};
            components.replicationMetadata =
                ReplicationMetadataComponent{WorldEntityKind::Player, 100, WorldShapeKind::Circle, 0.75f};
            components.playerControl = PlayerControlComponent{playerId};
            return components;
        }
    } // namespace

    TEST(WorldMovementPhaseTests, WholeSpanAndPartitionedSubspansProduceSameReadOnlyResult)
    {
        WorldEntityManager entityManager;
        std::array<WorldEntityKey, 2> entityKeys;
        std::array<EntityHandle, 2> handles;
        ASSERT_TRUE(entityManager.TryCreate(MakePhaseComponents(7, 10.0f), &entityKeys[0], &handles[0]));
        ASSERT_TRUE(entityManager.TryCreate(MakePhaseComponents(8, 30.0f), &entityKeys[1], &handles[1]));

        const std::array<WorldMovementTickInput, 2> inputs{
            WorldMovementTickInput{WorldSessionKey{10}, 7, entityKeys[0], 1.0f, -0.5f},
            WorldMovementTickInput{WorldSessionKey{11}, 8, entityKeys[1], -0.25f, 0.75f},
        };
        const WorldReadView readView{entityManager};
        std::array<WorldMovementEntityUpdate, 2> wholeUpdates;
        std::array<WorldMovementEntityUpdate, 2> partitionedUpdates;

        ASSERT_EQ(WorldMovementPhase::Compute(inputs, 0.05f, readView, wholeUpdates),
                  WorldMovementPhaseComputeResult::Computed);
        ASSERT_EQ(WorldMovementPhase::Compute(std::span<const WorldMovementTickInput>{inputs}.first(1), 0.05f, readView,
                                              std::span<WorldMovementEntityUpdate>{partitionedUpdates}.first(1)),
                  WorldMovementPhaseComputeResult::Computed);
        ASSERT_EQ(WorldMovementPhase::Compute(std::span<const WorldMovementTickInput>{inputs}.subspan(1), 0.05f,
                                              readView,
                                              std::span<WorldMovementEntityUpdate>{partitionedUpdates}.subspan(1)),
                  WorldMovementPhaseComputeResult::Computed);

        EXPECT_EQ(partitionedUpdates, wholeUpdates);

        WorldEntityComponents firstCanonical;
        WorldEntityComponents secondCanonical;
        ASSERT_TRUE(entityManager.TryReadComponents(handles[0], &firstCanonical));
        ASSERT_TRUE(entityManager.TryReadComponents(handles[1], &secondCanonical));
        EXPECT_EQ(firstCanonical, MakePhaseComponents(7, 10.0f));
        EXPECT_EQ(secondCanonical, MakePhaseComponents(8, 30.0f));
    }

    TEST(WorldMovementPhaseTests, RejectsMismatchedResultSpanWithoutWriting)
    {
        WorldEntityManager entityManager;
        const WorldReadView readView{entityManager};
        const std::array<WorldMovementTickInput, 1> inputs{};
        std::array<WorldMovementEntityUpdate, 2> updates{};
        const std::array<WorldMovementEntityUpdate, 2> original = updates;

        EXPECT_EQ(WorldMovementPhase::Compute(inputs, 0.05f, readView, updates),
                  WorldMovementPhaseComputeResult::InvalidArgument);
        EXPECT_EQ(updates, original);
    }
} // namespace psnr::world::tests
