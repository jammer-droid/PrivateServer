#include "pch.h"

#include "WorldDeathDropAnchorPlanner.h"

#include <cstdint>
#include <vector>

namespace psnr::world::tests
{
    TEST(WorldDeathDropAnchorPlannerTests, DistributesSparseDropsAcrossNormalizedTrailLength)
    {
        WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(10);
        ASSERT_TRUE(result.Succeeded());
        BodyTrailComponent bodyTrail = result.TakeValue();
        for (std::uint32_t sampleIndex = 1; sampleIndex <= 10; ++sampleIndex)
        {
            ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{static_cast<float>(sampleIndex), 0.0f}));
        }
        WorldResult<std::vector<WorldDeathDropAnchor>> planResult =
            WorldDeathDropAnchorPlanner::Plan(0.0f, 0.0f, bodyTrail, 2);
        ASSERT_TRUE(planResult.Succeeded());
        const std::vector<WorldDeathDropAnchor> anchors = planResult.TakeValue();

        ASSERT_EQ(anchors.size(), 2u);
        EXPECT_EQ(anchors[0], (WorldDeathDropAnchor{0, 2.5f, 0.0f}));
        EXPECT_EQ(anchors[1], (WorldDeathDropAnchor{1, 7.5f, 0.0f}));
    }

    TEST(WorldDeathDropAnchorPlannerTests, InterpolatesNormalizedDistancesAcrossBentTrail)
    {
        WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(3);
        ASSERT_TRUE(result.Succeeded());
        BodyTrailComponent bodyTrail = result.TakeValue();
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{2.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{4.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{4.0f, 2.0f}));
        WorldResult<std::vector<WorldDeathDropAnchor>> planResult =
            WorldDeathDropAnchorPlanner::Plan(0.0f, 0.0f, bodyTrail, 7);
        ASSERT_TRUE(planResult.Succeeded());
        const std::vector<WorldDeathDropAnchor> anchors = planResult.TakeValue();

        ASSERT_EQ(anchors.size(), 7u);
        EXPECT_EQ(anchors[0].dropOrdinal, 0u);
        EXPECT_NEAR(anchors[0].positionX, 0.42857143f, 0.000001f);
        EXPECT_FLOAT_EQ(anchors[0].positionY, 0.0f);
        EXPECT_EQ(anchors[1].dropOrdinal, 1u);
        EXPECT_NEAR(anchors[1].positionX, 1.28571429f, 0.000001f);
        EXPECT_FLOAT_EQ(anchors[1].positionY, 0.0f);
        EXPECT_EQ(anchors[2].dropOrdinal, 2u);
        EXPECT_NEAR(anchors[2].positionX, 2.14285714f, 0.000001f);
        EXPECT_FLOAT_EQ(anchors[2].positionY, 0.0f);
        EXPECT_EQ(anchors[3].dropOrdinal, 3u);
        EXPECT_FLOAT_EQ(anchors[3].positionX, 3.0f);
        EXPECT_FLOAT_EQ(anchors[3].positionY, 0.0f);
        EXPECT_EQ(anchors[4].dropOrdinal, 4u);
        EXPECT_NEAR(anchors[4].positionX, 3.85714286f, 0.000001f);
        EXPECT_FLOAT_EQ(anchors[4].positionY, 0.0f);
        EXPECT_EQ(anchors[5].dropOrdinal, 5u);
        EXPECT_FLOAT_EQ(anchors[5].positionX, 4.0f);
        EXPECT_NEAR(anchors[5].positionY, 0.71428571f, 0.000001f);
        EXPECT_EQ(anchors[6].dropOrdinal, 6u);
        EXPECT_FLOAT_EQ(anchors[6].positionX, 4.0f);
        EXPECT_NEAR(anchors[6].positionY, 1.57142857f, 0.000001f);
    }
} // namespace psnr::world::tests
