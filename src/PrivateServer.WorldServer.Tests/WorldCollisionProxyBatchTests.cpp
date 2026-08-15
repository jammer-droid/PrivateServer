#include "pch.h"

#include "WorldCollisionProxyBatch.h"

#include <limits>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents CreatePlayerComponents(const float headX, const float headY)
        {
            WorldEntityComponents components;
            components.transform.positionX = headX;
            components.transform.positionY = headY;
            components.replicationMetadata = ReplicationMetadataComponent{
                WorldEntityKind::Player,
                1,
                WorldShapeKind::Circle,
                0.5f,
            };
            WorldResult<BodyTrailComponent> bodyTrailResult = CreateBodyTrailComponent(3);
            EXPECT_TRUE(bodyTrailResult.Succeeded());
            components.bodyTrail = bodyTrailResult.TakeValue();
            EXPECT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{headX, headY}));
            EXPECT_TRUE(components.bodyTrail.TryPushBack(BodyTrailSample{headX - 1.0f, headY}));
            return components;
        }
    } // namespace

    TEST(WorldCollisionProxyBatchTests, ValidatesCanonicalPlayerSpawnBounds)
    {
        EXPECT_TRUE(WorldPlayerSpawnBounds::IsValid(WorldPlayerSpawnBounds{-1.0f, -2.0f, 3.0f, 4.0f}));
        EXPECT_TRUE(WorldPlayerSpawnBounds::IsValid(WorldPlayerSpawnBounds{1.0f, 2.0f, 1.0f, 2.0f}));
        EXPECT_FALSE(WorldPlayerSpawnBounds::IsValid(WorldPlayerSpawnBounds{2.0f, -2.0f, 1.0f, 4.0f}));
        EXPECT_FALSE(WorldPlayerSpawnBounds::IsValid(
            WorldPlayerSpawnBounds{-1.0f, -2.0f, 3.0f, std::numeric_limits<float>::infinity()}));
    }

    TEST(WorldCollisionProxyBatchTests, AccumulatesEveryPlayerInOneFlatTickBatch)
    {
        const WorldEntityComponents firstPlayer = CreatePlayerComponents(4.0f, 2.0f);
        const WorldEntityComponents secondPlayer = CreatePlayerComponents(10.0f, 3.0f);
        WorldCollisionProxyBatch batch;

        ASSERT_EQ(batch.AppendPlayer(WorldEntityKey{7, 1}, firstPlayer), WorldCollisionProxyAppendResult::Appended);
        ASSERT_EQ(batch.AppendPlayer(WorldEntityKey{8, 2}, secondPlayer), WorldCollisionProxyAppendResult::Appended);

        const std::span<const WorldCollisionProxy> proxies = batch.Proxies();
        ASSERT_EQ(proxies.size(), 6u);
        EXPECT_EQ(proxies[0], (WorldCollisionProxy{WorldEntityKey{7, 1}, WorldCollisionProxyRole::PlayerHead, 0, 4.0f,
                                                   2.0f, 4.0f, 2.0f, 0.5f}));
        EXPECT_EQ(proxies[1], (WorldCollisionProxy{WorldEntityKey{7, 1}, WorldCollisionProxyRole::PlayerBody, 0, 4.0f,
                                                   2.0f, 4.0f, 2.0f, 0.5f}));
        EXPECT_EQ(proxies[2], (WorldCollisionProxy{WorldEntityKey{7, 1}, WorldCollisionProxyRole::PlayerBody, 1, 4.0f,
                                                   2.0f, 3.0f, 2.0f, 0.5f}));
        EXPECT_EQ(proxies[3].ownerKey, (WorldEntityKey{8, 2}));
        EXPECT_EQ(proxies[3].role, WorldCollisionProxyRole::PlayerHead);
        EXPECT_EQ(proxies[5].segmentOrdinal, 1u);
    }

    TEST(WorldCollisionProxyBatchTests, ClearRetainsStorageForNextTick)
    {
        const WorldEntityComponents player = CreatePlayerComponents(4.0f, 2.0f);
        WorldCollisionProxyBatch batch;
        ASSERT_EQ(batch.AppendPlayer(WorldEntityKey{7, 1}, player), WorldCollisionProxyAppendResult::Appended);
        const std::size_t retainedCapacity = batch.Capacity();
        ASSERT_GT(retainedCapacity, 0u);

        batch.Clear();

        EXPECT_EQ(batch.Size(), 0u);
        EXPECT_EQ(batch.Capacity(), retainedCapacity);
        EXPECT_TRUE(batch.Proxies().empty());
    }

    TEST(WorldCollisionProxyBatchTests, InvalidPlayerDoesNotChangeAccumulatedBatch)
    {
        const WorldEntityComponents validPlayer = CreatePlayerComponents(4.0f, 2.0f);
        WorldEntityComponents invalidPlayer = CreatePlayerComponents(10.0f, 3.0f);
        ASSERT_TRUE(
            invalidPlayer.bodyTrail.TryWrite(1, BodyTrailSample{std::numeric_limits<float>::quiet_NaN(), 3.0f}));
        WorldCollisionProxyBatch batch;
        ASSERT_EQ(batch.AppendPlayer(WorldEntityKey{7, 1}, validPlayer), WorldCollisionProxyAppendResult::Appended);
        const std::size_t unchangedSize = batch.Size();

        EXPECT_EQ(batch.AppendPlayer(WorldEntityKey{8, 1}, invalidPlayer),
                  WorldCollisionProxyAppendResult::InvalidEntityState);
        EXPECT_EQ(batch.Size(), unchangedSize);
        ASSERT_FALSE(batch.Proxies().empty());
        EXPECT_EQ(batch.Proxies()[0].ownerKey, (WorldEntityKey{7, 1}));
    }
} // namespace psnr::world
