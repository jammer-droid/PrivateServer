#include "pch.h"

#include "WorldDeathDropPlacementFilter.h"
#include "WorldResourceRegistry.h"

#include <vector>

namespace psnr::world::tests
{
    TEST(WorldDeathDropPlacementFilterTests, KeepsOnlyStrictlyContainedExactUniqueAnchors)
    {
        const WorldActiveArea activeArea{0.0f, 0.0f, 5.0f, 1.0f, 5.0f};
        WorldResourceRegistry resourceRegistry;
        ASSERT_EQ(resourceRegistry.TryRegister(WorldResourceInstance{WorldEntityKey{100, 1}, EntityHandle{0, 1},
                                                                     WorldResourceOrigin::Ambient, 1, 1.0f, 0.0f}),
                  WorldResourceRegisterResult::Registered);
        const std::vector<WorldDeathDropAnchor> candidates{
            WorldDeathDropAnchor{0, 0.0f, 0.0f}, WorldDeathDropAnchor{1, 1.0f, 0.0f},
            WorldDeathDropAnchor{2, 0.0f, 0.0f}, WorldDeathDropAnchor{3, 4.4f, 0.0f},
            WorldDeathDropAnchor{4, 4.5f, 0.0f}, WorldDeathDropAnchor{5, 1.001f, 0.0f},
        };
        WorldResult<WorldDeathDropPlacementPlan> result =
            WorldDeathDropPlacementFilter::Filter(activeArea, 0.5f, candidates, resourceRegistry, {});
        ASSERT_TRUE(result.Succeeded());
        const WorldDeathDropPlacementPlan plan = result.TakeValue();

        EXPECT_EQ(plan.rejectedCount, 3u);
        ASSERT_EQ(plan.acceptedAnchors.size(), 3u);
        EXPECT_EQ(plan.acceptedAnchors[0], (WorldDeathDropAnchor{0, 0.0f, 0.0f}));
        EXPECT_EQ(plan.acceptedAnchors[1], (WorldDeathDropAnchor{3, 4.4f, 0.0f}));
        EXPECT_EQ(plan.acceptedAnchors[2], (WorldDeathDropAnchor{5, 1.001f, 0.0f}));
    }
} // namespace psnr::world::tests
