#include "pch.h"

#include "WorldTickSampleBuffer.h"

#include <span>

namespace psnr::world::tests
{
    TEST(WorldTickSampleBufferTests, RejectsZeroCapacity)
    {
        const WorldResult<std::unique_ptr<WorldTickSampleBuffer>> result = WorldTickSampleBuffer::Create(0);

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidCapacity);
    }

    TEST(WorldTickSampleBufferTests, PreservesRecordedSamplesInTickOrder)
    {
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> createResult = WorldTickSampleBuffer::Create(2);
        ASSERT_TRUE(createResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> buffer = createResult.TakeValue();

        const WorldTickSample first{7, 100, 100, 3, WorldRoundPhase::Running, 1, 1, 250, 400};
        const WorldTickSample second{8, 101, 102, 3, WorldRoundPhase::Running, 2, 2, 1'000, 700};

        EXPECT_TRUE(buffer->TryRecord(first));
        EXPECT_TRUE(buffer->TryRecord(second));

        const std::span<const WorldTickSample> samples = buffer->Samples();
        ASSERT_EQ(samples.size(), 2);
        EXPECT_EQ(samples[0], first);
        EXPECT_EQ(samples[1], second);
        EXPECT_EQ(buffer->MaxSampleCount(), 2);
        EXPECT_EQ(buffer->SampleCount(), 2);
        EXPECT_EQ(buffer->DroppedSampleCount(), 0);
        EXPECT_FALSE(buffer->Empty());
        EXPECT_TRUE(buffer->Full());
    }

    TEST(WorldTickSampleBufferTests, CountsSamplesDroppedAfterCapacityIsReached)
    {
        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> createResult = WorldTickSampleBuffer::Create(1);
        ASSERT_TRUE(createResult.Succeeded());
        std::unique_ptr<WorldTickSampleBuffer> buffer = createResult.TakeValue();
        const WorldTickSample sample{1, 60, 60, 1, WorldRoundPhase::Running, 1, 1, 0, 100};

        EXPECT_TRUE(buffer->TryRecord(sample));
        EXPECT_FALSE(buffer->TryRecord(sample));
        EXPECT_FALSE(buffer->TryRecord(sample));

        EXPECT_EQ(buffer->SampleCount(), 1);
        EXPECT_EQ(buffer->DroppedSampleCount(), 2);
        ASSERT_EQ(buffer->Samples().size(), 1);
        EXPECT_EQ(buffer->Samples()[0], sample);
    }
} // namespace psnr::world::tests
