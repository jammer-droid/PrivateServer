#include "pch.h"

#include "WorldResourceSpawnPlanner.h"

#include <vector>

namespace psnr::world::tests
{
    TEST(WorldResourceSpawnPlannerTests, PlansStableAmbientRequestAndRetriesOccupiedPosition)
    {
        constexpr WorldActiveArea ActiveArea{10.0f, -5.0f, 100.0f, 0.5f, 50.0f};
        WorldResourceRegistry emptyRegistry;
        WorldResult<WorldResourceSpawnPlan> firstResult =
            WorldResourceSpawnPlanner::PlanAmbient(ActiveArea, 0.5f, 42, 3, emptyRegistry, {});
        WorldResult<WorldResourceSpawnPlan> repeatedResult =
            WorldResourceSpawnPlanner::PlanAmbient(ActiveArea, 0.5f, 42, 3, emptyRegistry, {});
        ASSERT_TRUE(firstResult.Succeeded());
        ASSERT_TRUE(repeatedResult.Succeeded());
        const WorldResourceSpawnPlan first = firstResult.TakeValue();
        const WorldResourceSpawnPlan repeated = repeatedResult.TakeValue();

        EXPECT_EQ(first, repeated);
        ASSERT_EQ(first.requests.size(), 1u);
        EXPECT_EQ(first.rejectedCount, 0u);
        const WorldResourceSpawnRequest& firstRequest = first.requests[0];
        EXPECT_EQ(firstRequest.origin, WorldResourceOrigin::Ambient);
        EXPECT_EQ(firstRequest.ambientSlotId, 3u);
        EXPECT_FALSE(firstRequest.sourceEntityKey.IsValid());
        EXPECT_EQ(firstRequest.sourceOrdinal, 0u);
        EXPECT_TRUE(ActiveArea.ContainsCircleStrictly(firstRequest.positionX, firstRequest.positionY, 0.5f));

        WorldResourceRegistry occupiedRegistry;
        ASSERT_EQ(occupiedRegistry.TryRegister(WorldResourceInstance{WorldEntityKey{100, 1}, EntityHandle{0, 1},
                                                                     WorldResourceOrigin::DeathDrop, 0,
                                                                     firstRequest.positionX, firstRequest.positionY}),
                  WorldResourceRegisterResult::Registered);
        WorldResult<WorldResourceSpawnPlan> retriedResult =
            WorldResourceSpawnPlanner::PlanAmbient(ActiveArea, 0.5f, 42, 3, occupiedRegistry, {});
        ASSERT_TRUE(retriedResult.Succeeded());
        const WorldResourceSpawnPlan retried = retriedResult.TakeValue();

        ASSERT_EQ(retried.requests.size(), 1u);
        EXPECT_EQ(retried.requests[0].sourceOrdinal, 1u);
        EXPECT_TRUE(retried.requests[0].positionX != firstRequest.positionX ||
                    retried.requests[0].positionY != firstRequest.positionY);
    }

    TEST(WorldResourceSpawnPlannerTests, TriesEightAmbientCandidatesBeforeReportingNoUniquePosition)
    {
        constexpr WorldActiveArea ActiveArea{10.0f, -5.0f, 100.0f, 0.5f, 50.0f};
        WorldResourceRegistry emptyRegistry;
        std::vector<WorldResourcePosition> reservedPositions;

        for (std::uint32_t expectedOrdinal = 0; expectedOrdinal < 8; ++expectedOrdinal)
        {
            WorldResult<WorldResourceSpawnPlan> result =
                WorldResourceSpawnPlanner::PlanAmbient(ActiveArea, 0.5f, 42, 3, emptyRegistry, reservedPositions);
            ASSERT_TRUE(result.Succeeded());
            const WorldResourceSpawnPlan plan = result.TakeValue();
            ASSERT_EQ(plan.requests.size(), 1u);
            EXPECT_EQ(plan.requests[0].sourceOrdinal, expectedOrdinal);
            reservedPositions.push_back(WorldResourcePosition{plan.requests[0].positionX, plan.requests[0].positionY});
        }

        const WorldResult<WorldResourceSpawnPlan> exhaustedResult =
            WorldResourceSpawnPlanner::PlanAmbient(ActiveArea, 0.5f, 42, 3, emptyRegistry, reservedPositions);
        ASSERT_TRUE(exhaustedResult.Failed());
        EXPECT_EQ(exhaustedResult.Error(), WorldErrorCode::CapacityExceeded);
    }

    TEST(WorldResourceSpawnPlannerTests, PlansDeathDropRequestsThroughCommonResult)
    {
        WorldResult<BodyTrailComponent> bodyTrailResult = CreateBodyTrailComponent(3);
        ASSERT_TRUE(bodyTrailResult.Succeeded());
        BodyTrailComponent bodyTrail = bodyTrailResult.TakeValue();
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{2.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{4.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{4.0f, 2.0f}));
        const WorldActiveArea activeArea{0.0f, 0.0f, 5.0f, 1.0f, 5.0f};
        WorldResourceRegistry resourceRegistry;
        ASSERT_EQ(resourceRegistry.TryRegister(WorldResourceInstance{WorldEntityKey{100, 1}, EntityHandle{0, 1},
                                                                     WorldResourceOrigin::Ambient, 1, 1.0f, 0.0f}),
                  WorldResourceRegisterResult::Registered);
        const std::vector<WorldResourcePosition> reservedPositions{WorldResourcePosition{3.0f, 0.0f}};
        constexpr WorldEntityKey SourceEntityKey{900, 1};
        WorldResult<WorldResourceSpawnPlan> result = WorldResourceSpawnPlanner::PlanDeathDrop(
            activeArea, 0.5f, SourceEntityKey, 0.0f, 0.0f, bodyTrail, 3, resourceRegistry, reservedPositions);
        ASSERT_TRUE(result.Succeeded());
        const WorldResourceSpawnPlan plan = result.TakeValue();

        EXPECT_EQ(plan.rejectedCount, 2u);
        ASSERT_EQ(plan.requests.size(), 1u);
        EXPECT_EQ(plan.requests[0],
                  (WorldResourceSpawnRequest{WorldResourceOrigin::DeathDrop, 0, SourceEntityKey, 2, 4.0f, 1.0f}));
    }

    TEST(WorldResourceSpawnPlannerTests, RejectsMissingAmbientPlacementArea)
    {
        constexpr WorldActiveArea ActiveArea{0.0f, 0.0f, 10.0f, 0.05f, 0.5f};
        WorldResourceRegistry resourceRegistry;
        const WorldResult<WorldResourceSpawnPlan> result =
            WorldResourceSpawnPlanner::PlanAmbient(ActiveArea, 0.5f, 42, 3, resourceRegistry, {});
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world::tests
