#include "pch.h"

#include "WorldSpatialIndex.h"

#include <limits>
#include <memory>
#include <vector>

namespace psnr::world
{
    namespace
    {
        constexpr WorldSpatialConfig ValidSpatialConfig{1.0f, 1.0f, 2.0f};

        [[nodiscard]] WorldSpatialProxy CreateSpatialProxy(const std::uint32_t entityId,
                                                           const WorldEntityKind entityKind, const float centerX,
                                                           const float centerY, const float radius)
        {
            return WorldSpatialProxy{
                WorldEntityKey{entityId, 1}, entityKind, centerX, centerY, radius,
            };
        }

        [[nodiscard]] std::unique_ptr<WorldSpatialIndex> CreateTestIndex()
        {
            WorldResult<std::unique_ptr<WorldSpatialIndex>> result = WorldSpatialIndex::Create(ValidSpatialConfig);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }
    } // namespace

    TEST(WorldSpatialIndexTests, OrdersSpatialProxiesByCanonicalEntityKey)
    {
        const WorldSpatialProxyEntityKeyLess less;
        const WorldSpatialProxy first = CreateSpatialProxy(1, WorldEntityKind::Player, 0.0f, 0.0f, 0.25f);
        const WorldSpatialProxy second = CreateSpatialProxy(2, WorldEntityKind::Player, 0.0f, 0.0f, 0.25f);
        WorldSpatialProxy newerGeneration = first;
        newerGeneration.entityKey.generation = 2;

        EXPECT_TRUE(less(first, newerGeneration));
        EXPECT_TRUE(less(newerGeneration, second));
        EXPECT_FALSE(less(second, first));
    }

    TEST(WorldSpatialIndexTests, CreatesOnlyFromValidExplicitConfig)
    {
        WorldResult<std::unique_ptr<WorldSpatialIndex>> result = WorldSpatialIndex::Create(ValidSpatialConfig);
        ASSERT_TRUE(result.Succeeded());
        std::unique_ptr<WorldSpatialIndex> index = result.TakeValue();
        ASSERT_NE(index, nullptr);
        EXPECT_EQ(index->Config(), ValidSpatialConfig);

        WorldSpatialConfig invalidCellSize = ValidSpatialConfig;
        invalidCellSize.spatialCellSize = 0.0f;
        WorldSpatialConfig invalidEnterRadius = ValidSpatialConfig;
        invalidEnterRadius.aoiEnterRadius = std::numeric_limits<float>::infinity();
        WorldSpatialConfig invalidRetainRadius = ValidSpatialConfig;
        invalidRetainRadius.aoiRetainRadius = invalidRetainRadius.aoiEnterRadius;
        const WorldResult<std::unique_ptr<WorldSpatialIndex>> invalidCellResult =
            WorldSpatialIndex::Create(invalidCellSize);
        const WorldResult<std::unique_ptr<WorldSpatialIndex>> invalidEnterResult =
            WorldSpatialIndex::Create(invalidEnterRadius);
        const WorldResult<std::unique_ptr<WorldSpatialIndex>> invalidRetainResult =
            WorldSpatialIndex::Create(invalidRetainRadius);

        ASSERT_TRUE(invalidCellResult.Failed());
        ASSERT_TRUE(invalidEnterResult.Failed());
        ASSERT_TRUE(invalidRetainResult.Failed());
        EXPECT_EQ(invalidCellResult.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(invalidEnterResult.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(invalidRetainResult.Error(), WorldErrorCode::InvalidConfig);
    }

    TEST(WorldSpatialIndexTests, RegistersFootprintCellsAndAppliesExactCircleQuery)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateTestIndex();
        ASSERT_NE(index, nullptr);
        const std::vector<WorldSpatialProxy> proxies{
            CreateSpatialProxy(3, WorldEntityKind::Resource, 3.0f, 0.0f, 0.2f),
            CreateSpatialProxy(1, WorldEntityKind::Player, 0.9f, 0.0f, 0.1f),
            CreateSpatialProxy(2, WorldEntityKind::Resource, 2.4f, 0.0f, 0.6f),
        };

        ASSERT_EQ(index->Rebuild(proxies), WorldSpatialIndexBuildResult::Built);
        EXPECT_EQ(index->ProxyCount(), 3u);
        EXPECT_GT(index->OccupiedCellCount(), index->ProxyCount());

        std::vector<WorldEntityKey> entered;
        std::vector<WorldEntityKey> retained;
        ASSERT_EQ(index->QueryVisibilityBands(WorldEntityKey{1, 1}, &entered, &retained),
                  WorldSpatialQueryResult::Queried);
        ASSERT_EQ(entered.size(), 1u);
        EXPECT_EQ(entered[0], (WorldEntityKey{2, 1}));
        EXPECT_EQ(retained, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}, WorldEntityKey{3, 1}}));
    }

    TEST(WorldSpatialIndexTests, AcceptsOwnedProxyStorageForMoveRebuild)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateTestIndex();
        ASSERT_NE(index, nullptr);
        std::vector<WorldSpatialProxy> proxies{
            CreateSpatialProxy(1, WorldEntityKind::Player, 0.0f, 0.0f, 0.25f),
            CreateSpatialProxy(2, WorldEntityKind::Resource, 0.5f, 0.0f, 0.25f),
        };

        ASSERT_EQ(index->Rebuild(std::move(proxies)), WorldSpatialIndexBuildResult::Built);
        EXPECT_EQ(index->ProxyCount(), 2u);
    }

    TEST(WorldSpatialIndexTests, ReturnsStableUniqueKeysAndKeepsPreviousBuildOnInvalidInput)
    {
        const std::unique_ptr<WorldSpatialIndex> index = CreateTestIndex();
        ASSERT_NE(index, nullptr);
        const std::vector<WorldSpatialProxy> firstBuild{
            CreateSpatialProxy(3, WorldEntityKind::Resource, 0.5f, 0.0f, 0.75f),
            CreateSpatialProxy(1, WorldEntityKind::Player, 0.0f, 0.0f, 0.25f),
            CreateSpatialProxy(2, WorldEntityKind::Resource, -0.5f, 0.0f, 0.75f),
        };
        ASSERT_EQ(index->Rebuild(firstBuild), WorldSpatialIndexBuildResult::Built);

        std::vector<WorldEntityKey> entered;
        std::vector<WorldEntityKey> retained;
        ASSERT_EQ(index->QueryVisibilityBands(WorldEntityKey{1, 1}, &entered, &retained),
                  WorldSpatialQueryResult::Queried);
        EXPECT_EQ(entered, (std::vector<WorldEntityKey>{WorldEntityKey{2, 1}, WorldEntityKey{3, 1}}));

        const std::vector<WorldSpatialProxy> duplicateKeys{
            CreateSpatialProxy(1, WorldEntityKind::Player, 0.0f, 0.0f, 0.25f),
            CreateSpatialProxy(1, WorldEntityKind::Resource, 1.0f, 0.0f, 0.25f),
        };
        EXPECT_EQ(index->Rebuild(duplicateKeys), WorldSpatialIndexBuildResult::InvalidInput);

        std::vector<WorldEntityKey> enteredAfterFailure;
        std::vector<WorldEntityKey> retainedAfterFailure;
        ASSERT_EQ(index->QueryVisibilityBands(WorldEntityKey{1, 1}, &enteredAfterFailure, &retainedAfterFailure),
                  WorldSpatialQueryResult::Queried);
        EXPECT_EQ(enteredAfterFailure, entered);
        EXPECT_EQ(retainedAfterFailure, retained);
    }
} // namespace psnr::world
