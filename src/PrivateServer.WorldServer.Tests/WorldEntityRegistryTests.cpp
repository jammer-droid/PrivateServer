#include "pch.h"

#include "WorldEntityRegistry.h"

namespace psnr::world::tests
{
    TEST(WorldEntityRegistryTests, CreatesAndResolvesIdentityInBothDirections)
    {
        WorldEntityRegistry registry;
        WorldEntityKey createdKey{};
        EntityHandle created{};
        EntityHandle foundHandle{};
        WorldEntityKey foundKey{};

        ASSERT_TRUE(registry.TryCreate(&createdKey, &created));
        EXPECT_EQ(createdKey, (WorldEntityKey{1, 1}));
        EXPECT_EQ(created.slotIndex, 0u);
        EXPECT_EQ(created.slotGeneration, 1u);
        ASSERT_TRUE(registry.TryFindHandle(createdKey, &foundHandle));
        EXPECT_EQ(foundHandle, created);
        ASSERT_TRUE(registry.TryFindKey(created, &foundKey));
        EXPECT_EQ(foundKey, createdKey);
        EXPECT_EQ(registry.Size(), 1u);
    }

    TEST(WorldEntityRegistryTests, RejectsInvalidOutputsWithoutConsumingSlot)
    {
        WorldEntityRegistry registry;
        constexpr WorldEntityKey UnchangedKey{99, 9};
        constexpr EntityHandle UnchangedHandle{99, 9};
        WorldEntityKey key = UnchangedKey;
        EntityHandle handle = UnchangedHandle;

        EXPECT_FALSE(registry.TryCreate(nullptr, &handle));
        EXPECT_EQ(handle, UnchangedHandle);
        EXPECT_FALSE(registry.TryCreate(&key, nullptr));
        EXPECT_EQ(key, UnchangedKey);
        EXPECT_EQ(registry.Size(), 0u);

        ASSERT_TRUE(registry.TryCreate(&key, &handle));
        EXPECT_EQ(key, (WorldEntityKey{1, 1}));
        EXPECT_EQ(handle, (EntityHandle{0, 1}));
        EXPECT_FALSE(registry.TryFindHandle(key, nullptr));
        EXPECT_FALSE(registry.TryFindKey(handle, nullptr));
        EXPECT_EQ(registry.Size(), 1u);
    }

    TEST(WorldEntityRegistryTests, RemovalInvalidatesHandleAndReturnsSlotForReuse)
    {
        WorldEntityRegistry registry;
        WorldEntityKey removedKey{};
        WorldEntityKey replacementKey{};
        EntityHandle removedHandle{};
        EntityHandle replacementHandle{};
        EntityHandle foundHandle{};
        WorldEntityKey foundKey{};

        ASSERT_TRUE(registry.TryCreate(&removedKey, &removedHandle));
        ASSERT_TRUE(registry.Remove(removedHandle));

        EXPECT_FALSE(registry.TryFindHandle(removedKey, &foundHandle));
        EXPECT_FALSE(registry.TryFindKey(removedHandle, &foundKey));
        EXPECT_FALSE(registry.Remove(removedHandle));
        EXPECT_EQ(registry.Size(), 0u);

        ASSERT_TRUE(registry.TryCreate(&replacementKey, &replacementHandle));
        EXPECT_EQ(replacementKey.entityId, removedKey.entityId);
        EXPECT_EQ(replacementKey.generation, removedKey.generation + 1);
        EXPECT_EQ(replacementHandle.slotIndex, removedHandle.slotIndex);
        EXPECT_EQ(replacementHandle.slotGeneration, removedHandle.slotGeneration + 1);
        EXPECT_FALSE(registry.TryFindKey(removedHandle, &foundKey));
        ASSERT_TRUE(registry.TryFindKey(replacementHandle, &foundKey));
        EXPECT_EQ(foundKey, replacementKey);
    }

    TEST(WorldEntityRegistryTests, ReusesFreeSlotsInLastInFirstOutOrder)
    {
        WorldEntityRegistry registry;
        WorldEntityKey firstKey{};
        WorldEntityKey secondKey{};
        WorldEntityKey reusedKey{};
        EntityHandle first{};
        EntityHandle second{};
        EntityHandle reused{};

        ASSERT_TRUE(registry.TryCreate(&firstKey, &first));
        ASSERT_TRUE(registry.TryCreate(&secondKey, &second));
        ASSERT_TRUE(registry.Remove(first));
        ASSERT_TRUE(registry.Remove(second));

        ASSERT_TRUE(registry.TryCreate(&reusedKey, &reused));
        EXPECT_EQ(reusedKey.entityId, secondKey.entityId);
        EXPECT_EQ(reusedKey.generation, secondKey.generation + 1);
        EXPECT_EQ(reused.slotIndex, second.slotIndex);
        EXPECT_EQ(reused.slotGeneration, second.slotGeneration + 1);
    }
} // namespace psnr::world::tests
