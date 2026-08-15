#include "pch.h"

#include "WorldBodyTrailComponent.h"

namespace psnr::world::tests
{
    TEST(WorldBodyTrailComponentTests, PreservesLogicalOrderAcrossRingWrap)
    {
        WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(4);
        ASSERT_TRUE(result.Succeeded());
        BodyTrailComponent bodyTrail = result.TakeValue();
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{1.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{2.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{3.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryTrimBack(1));
        ASSERT_TRUE(bodyTrail.TryPushFront(BodyTrailSample{4.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushFront(BodyTrailSample{5.0f, 0.0f}));

        ASSERT_EQ(bodyTrail.SampleCount(), 3u);
        BodyTrailSample sample;
        ASSERT_TRUE(bodyTrail.TryRead(0, &sample));
        EXPECT_EQ(sample, (BodyTrailSample{5.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryRead(1, &sample));
        EXPECT_EQ(sample, (BodyTrailSample{4.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryRead(2, &sample));
        EXPECT_EQ(sample, (BodyTrailSample{1.0f, 0.0f}));
    }

    TEST(WorldBodyTrailComponentTests, RejectsOverflowWithoutOverwritingTail)
    {
        WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(2);
        ASSERT_TRUE(result.Succeeded());
        BodyTrailComponent bodyTrail = result.TakeValue();
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{1.0f, 0.0f}));
        ASSERT_TRUE(bodyTrail.TryPushBack(BodyTrailSample{2.0f, 0.0f}));
        const BodyTrailComponent unchanged = bodyTrail;

        EXPECT_FALSE(bodyTrail.TryPushFront(BodyTrailSample{3.0f, 0.0f}));
        EXPECT_EQ(bodyTrail, unchanged);
    }

    TEST(WorldBodyTrailComponentTests, RejectsInvalidCapacity)
    {
        const WorldResult<BodyTrailComponent> result = CreateBodyTrailComponent(0);

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidCapacity);
    }
} // namespace psnr::world::tests
