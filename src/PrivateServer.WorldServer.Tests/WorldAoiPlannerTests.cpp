#include "pch.h"

#include "WorldAoiPlanner.h"

#include <memory>
#include <vector>

namespace psnr::world
{
    namespace
    {
        constexpr WorldSpatialConfig AoiConfig{1.0f, 3.0f, 4.0f};

        [[nodiscard]] WorldSpatialProxy CreateAoiProxy(const std::uint32_t entityId, const WorldEntityKind entityKind,
                                                       const float centerX)
        {
            return WorldSpatialProxy{
                WorldEntityKey{entityId, 1}, entityKind, centerX, 0.0f, 0.5f,
            };
        }

        [[nodiscard]] std::unique_ptr<WorldSpatialIndex> CreateAoiIndex()
        {
            WorldResult<std::unique_ptr<WorldSpatialIndex>> result = WorldSpatialIndex::Create(AoiConfig);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }
    } // namespace

    TEST(WorldAoiPlannerTests, CommitsStableEnterStayLeaveDiffsAndRemovesSessionState)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateAoiIndex();
        ASSERT_NE(index, nullptr);
        WorldAoiPlanner planner;
        const WorldAoiRecipient recipient{WorldSessionKey{10}, WorldEntityKey{1, 1}};
        std::vector<WorldAoiVisibilityDiff> diffs;

        const std::vector<WorldSpatialProxy> firstTick{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 2.0f),
            CreateAoiProxy(3, WorldEntityKind::StaticObstacle, 5.0f),
        };
        ASSERT_EQ(index->Rebuild(firstTick), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        ASSERT_EQ(diffs.size(), 1u);
        EXPECT_EQ(diffs[0].entered, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));
        EXPECT_TRUE(diffs[0].stayed.empty());
        EXPECT_TRUE(diffs[0].left.empty());

        const std::vector<WorldSpatialProxy> secondTick{
            CreateAoiProxy(3, WorldEntityKind::StaticObstacle, 2.8f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 2.5f),
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
        };
        ASSERT_EQ(index->Rebuild(secondTick), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        ASSERT_EQ(diffs.size(), 1u);
        EXPECT_EQ(diffs[0].entered, (std::vector<WorldEntityKey>{WorldEntityKey{3, 1}}));
        EXPECT_EQ(diffs[0].stayed, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));
        EXPECT_TRUE(diffs[0].left.empty());

        const std::vector<WorldSpatialProxy> thirdTick{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 8.0f),
            CreateAoiProxy(3, WorldEntityKind::StaticObstacle, 2.8f),
        };
        ASSERT_EQ(index->Rebuild(thirdTick), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        ASSERT_EQ(diffs.size(), 1u);
        EXPECT_TRUE(diffs[0].entered.empty());
        EXPECT_EQ(diffs[0].stayed, (std::vector<WorldEntityKey>{WorldEntityKey{3, 1}}));
        EXPECT_EQ(diffs[0].left, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));
        const std::span<const WorldEntityKey> committedVisible = planner.VisibleEntities(recipient.sessionKey);
        ASSERT_EQ(committedVisible.size(), 1u);
        EXPECT_EQ(committedVisible[0], diffs[0].stayed[0]);

        EXPECT_TRUE(planner.RemoveSession(recipient.sessionKey));
        EXPECT_EQ(planner.RecipientCount(), 0u);
        EXPECT_TRUE(planner.VisibleEntities(recipient.sessionKey).empty());
    }

    TEST(WorldAoiPlannerTests, RejectsDuplicateRecipientsWithoutChangingCommittedState)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateAoiIndex();
        ASSERT_NE(index, nullptr);
        const std::vector<WorldSpatialProxy> proxies{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 2.0f),
        };
        ASSERT_EQ(index->Rebuild(proxies), WorldSpatialIndexBuildResult::Built);

        WorldAoiPlanner planner;
        const WorldAoiRecipient recipient{WorldSessionKey{10}, WorldEntityKey{1, 1}};
        std::vector<WorldAoiVisibilityDiff> diffs;
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);

        const std::vector<WorldAoiRecipient> duplicateRecipients{recipient, recipient};
        const std::vector<WorldAoiVisibilityDiff> unchangedDiffs = diffs;
        EXPECT_EQ(planner.PlanAndCommit(duplicateRecipients, *index, &diffs), WorldAoiPlanResult::InvalidInput);
        EXPECT_EQ(diffs, unchangedDiffs);
        const std::span<const WorldEntityKey> committedVisible = planner.VisibleEntities(recipient.sessionKey);
        ASSERT_EQ(committedVisible.size(), 1u);
        EXPECT_EQ(committedVisible[0], unchangedDiffs[0].entered[0]);
    }

    TEST(WorldAoiPlannerTests, UsesEnterAndRetainRadiiAsVisibilityHysteresis)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateAoiIndex();
        ASSERT_NE(index, nullptr);
        const WorldAoiRecipient recipient{WorldSessionKey{10}, WorldEntityKey{1, 1}};
        WorldAoiPlanner planner;
        std::vector<WorldAoiVisibilityDiff> diffs;

        const std::vector<WorldSpatialProxy> outsideEnter{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 3.75f),
        };
        ASSERT_EQ(index->Rebuild(outsideEnter), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        EXPECT_TRUE(diffs[0].entered.empty());

        const std::vector<WorldSpatialProxy> insideEnter{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 3.0f),
        };
        ASSERT_EQ(index->Rebuild(insideEnter), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        EXPECT_EQ(diffs[0].entered, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));

        ASSERT_EQ(index->Rebuild(outsideEnter), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        EXPECT_EQ(diffs[0].stayed, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));

        const std::vector<WorldSpatialProxy> outsideRetain{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 4.75f),
        };
        ASSERT_EQ(index->Rebuild(outsideRetain), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        EXPECT_EQ(diffs[0].left, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));
    }

    TEST(WorldAoiPlannerTests, PreservesRecipientsNotIncludedInCurrentPlan)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateAoiIndex();
        ASSERT_NE(index, nullptr);
        std::vector<WorldSpatialProxy> proxies{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 2.0f),
        };
        ASSERT_EQ(index->Rebuild(std::move(proxies)), WorldSpatialIndexBuildResult::Built);

        WorldAoiPlanner planner;
        const WorldAoiRecipient recipient{WorldSessionKey{10}, WorldEntityKey{1, 1}};
        std::vector<WorldAoiVisibilityDiff> diffs;
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        ASSERT_EQ(planner.RecipientCount(), 1u);

        ASSERT_EQ(planner.PlanAndCommit({}, *index, &diffs), WorldAoiPlanResult::Planned);
        EXPECT_TRUE(diffs.empty());
        EXPECT_EQ(planner.RecipientCount(), 1u);
        EXPECT_FALSE(planner.VisibleEntities(recipient.sessionKey).empty());
    }

    TEST(WorldAoiPlannerTests, TreatsGenerationReplacementAsLeaveThenEnter)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateAoiIndex();
        ASSERT_NE(index, nullptr);
        const WorldAoiRecipient recipient{WorldSessionKey{10}, WorldEntityKey{1, 1}};
        WorldAoiPlanner planner;
        std::vector<WorldAoiVisibilityDiff> diffs;
        const std::vector<WorldSpatialProxy> firstGeneration{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            WorldSpatialProxy{WorldEntityKey{2, 1}, WorldEntityKind::Resource, 2.0f, 0.0f, 0.5f},
        };
        ASSERT_EQ(index->Rebuild(firstGeneration), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);

        const std::vector<WorldSpatialProxy> secondGeneration{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            WorldSpatialProxy{WorldEntityKey{2, 2}, WorldEntityKind::Resource, 2.0f, 0.0f, 0.5f},
        };
        ASSERT_EQ(index->Rebuild(secondGeneration), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        ASSERT_EQ(diffs.size(), 1u);
        EXPECT_EQ(diffs[0].left, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}}));
        EXPECT_EQ(diffs[0].entered, (std::vector<WorldEntityKey>{WorldEntityKey{2, 2}}));
        EXPECT_TRUE(diffs[0].stayed.empty());
    }

    TEST(WorldAoiPlannerTests, AuthoritativePrunePreventsDuplicateLeftAoi)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateAoiIndex();
        ASSERT_NE(index, nullptr);
        const WorldAoiRecipient recipient{WorldSessionKey{10}, WorldEntityKey{1, 1}};
        WorldAoiPlanner planner;
        std::vector<WorldAoiVisibilityDiff> diffs;
        const std::vector<WorldSpatialProxy> visibleResource{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
            CreateAoiProxy(2, WorldEntityKind::Resource, 2.0f),
        };
        ASSERT_EQ(index->Rebuild(visibleResource), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);

        const WorldEntityKey removedKeys[] = {WorldEntityKey{2, 1}};
        std::vector<WorldAoiPrunedVisibility> pruned;
        ASSERT_EQ(planner.PruneVisibleEntities(removedKeys, &pruned), WorldAoiPlanResult::Planned);
        EXPECT_EQ(pruned, (std::vector<WorldAoiPrunedVisibility>{
                              WorldAoiPrunedVisibility{recipient.sessionKey, WorldEntityKey{2, 1}},
                          }));
        EXPECT_TRUE(planner.VisibleEntities(recipient.sessionKey).empty());

        const std::vector<WorldSpatialProxy> afterRemoval{
            CreateAoiProxy(1, WorldEntityKind::Player, 0.0f),
        };
        ASSERT_EQ(index->Rebuild(afterRemoval), WorldSpatialIndexBuildResult::Built);
        ASSERT_EQ(planner.PlanAndCommit(std::span<const WorldAoiRecipient>{&recipient, 1}, *index, &diffs),
                  WorldAoiPlanResult::Planned);
        ASSERT_EQ(diffs.size(), 1u);
        EXPECT_TRUE(diffs[0].left.empty());
    }
} // namespace psnr::world
