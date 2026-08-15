#include "pch.h"

#include "WorldExecutionStorage.h"

#include <memory>

namespace psnr::world::tests
{
    TEST(WorldExecutionStorageTests, AllocatesOnlyBuffersSelectedAtStartup)
    {
        WorldResult<std::unique_ptr<WorldExecutionStorage>> doubleBufferedResult =
            WorldExecutionStorage::Create(WorldExecutionStorageConfig{
                WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered},
                8,
                WorldOutboundBatchCapacity{4, 8, 64},
                WorldOutboundBufferSlotCount::Triple,
            });
        ASSERT_TRUE(doubleBufferedResult.Succeeded());
        std::unique_ptr<WorldExecutionStorage> doubleBuffered = doubleBufferedResult.TakeValue();
        ASSERT_NE(doubleBuffered, nullptr);
        EXPECT_NE(doubleBuffered->InboundBuffer(), nullptr);
        ASSERT_NE(doubleBuffered->OutboundBuffer(), nullptr);
        EXPECT_EQ(doubleBuffered->OutboundBuffer()->ConfiguredSlotCount(), 3u);

        WorldResult<std::unique_ptr<WorldExecutionStorage>> directResult =
            WorldExecutionStorage::Create(WorldExecutionStorageConfig{
                WorldExecutionModeConfig{WorldInboundMode::TargetServerTick, WorldOutboundMode::Direct},
                0,
                WorldOutboundBatchCapacity{},
            });
        ASSERT_TRUE(directResult.Succeeded());
        std::unique_ptr<WorldExecutionStorage> direct = directResult.TakeValue();
        ASSERT_NE(direct, nullptr);
        EXPECT_EQ(direct->InboundBuffer(), nullptr);
        EXPECT_EQ(direct->OutboundBuffer(), nullptr);
    }

    TEST(WorldExecutionStorageTests, InvalidSelectedCapacityRollsBackWholeCreation)
    {
        const WorldResult<std::unique_ptr<WorldExecutionStorage>> result =
            WorldExecutionStorage::Create(WorldExecutionStorageConfig{
                WorldExecutionModeConfig{WorldInboundMode::DoubleBuffered, WorldOutboundMode::DoubleBuffered},
                8,
                WorldOutboundBatchCapacity{},
            });

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidArgument);
    }
} // namespace psnr::world::tests
