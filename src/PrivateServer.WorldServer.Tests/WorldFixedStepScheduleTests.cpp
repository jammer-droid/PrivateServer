#include "pch.h"

#include "WorldFixedStepSchedule.h"

#include <chrono>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldFixedStepSchedule CreateSchedule(const std::uint32_t tickRateHz,
                                                            const std::uint32_t maxCatchUpSteps,
                                                            const std::uint32_t firstServerTick,
                                                            const WorldFixedStepSchedule::Clock::time_point startedAt)
        {
            const WorldFixedStepScheduleConfig config{
                tickRateHz,
                maxCatchUpSteps,
                firstServerTick,
            };
            WorldResult<WorldFixedStepSchedule> result = CreateWorldFixedStepSchedule(config, startedAt);
            EXPECT_TRUE(result.Succeeded());
            return result.TakeValue();
        }
    } // namespace

    TEST(WorldFixedStepScheduleTests, RejectsInvalidConfig)
    {
        const WorldFixedStepSchedule::Clock::time_point startedAt{};
        const WorldResult<WorldFixedStepSchedule> zeroTickRateResult =
            CreateWorldFixedStepSchedule(WorldFixedStepScheduleConfig{0, 4, 0}, startedAt);
        const WorldResult<WorldFixedStepSchedule> zeroCatchUpResult =
            CreateWorldFixedStepSchedule(WorldFixedStepScheduleConfig{30, 0, 0}, startedAt);

        ASSERT_TRUE(zeroTickRateResult.Failed());
        ASSERT_TRUE(zeroCatchUpResult.Failed());
        EXPECT_EQ(zeroTickRateResult.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(zeroCatchUpResult.Error(), WorldErrorCode::InvalidConfig);
    }

    TEST(WorldFixedStepScheduleTests, DoesNotTakeTickBeforeFirstDeadline)
    {
        const WorldFixedStepSchedule::Clock::time_point startedAt{};
        WorldFixedStepSchedule schedule = CreateSchedule(10, 4, 50, startedAt);
        const WorldFixedStepBatch unchanged{77, 88, true};
        WorldFixedStepBatch batch = unchanged;

        EXPECT_EQ(schedule.TryTakeDueTicks(startedAt + std::chrono::milliseconds{99}, &batch),
                  WorldFixedStepTakeResult::NotDue);
        EXPECT_EQ(batch.firstServerTick, unchanged.firstServerTick);
        EXPECT_EQ(batch.stepCount, unchanged.stepCount);
        EXPECT_EQ(batch.overrun, unchanged.overrun);
    }

    TEST(WorldFixedStepScheduleTests, TakesSequentialTicksAtMonotonicDeadlines)
    {
        const WorldFixedStepSchedule::Clock::time_point startedAt{};
        WorldFixedStepSchedule schedule = CreateSchedule(10, 4, 50, startedAt);
        WorldFixedStepBatch firstBatch;
        WorldFixedStepBatch secondBatch;

        ASSERT_EQ(schedule.TryTakeDueTicks(startedAt + std::chrono::milliseconds{100}, &firstBatch),
                  WorldFixedStepTakeResult::Taken);
        EXPECT_EQ(firstBatch.firstServerTick, 50u);
        EXPECT_EQ(firstBatch.stepCount, 1u);
        EXPECT_FALSE(firstBatch.overrun);
        EXPECT_EQ(schedule.NextDeadline(), startedAt + std::chrono::milliseconds{200});

        ASSERT_EQ(schedule.TryTakeDueTicks(startedAt + std::chrono::milliseconds{300}, &secondBatch),
                  WorldFixedStepTakeResult::Taken);
        EXPECT_EQ(secondBatch.firstServerTick, 51u);
        EXPECT_EQ(secondBatch.stepCount, 2u);
        EXPECT_FALSE(secondBatch.overrun);
        EXPECT_EQ(schedule.NextDeadline(), startedAt + std::chrono::milliseconds{400});
    }

    TEST(WorldFixedStepScheduleTests, BoundsCatchUpWithoutSkippingOverdueTicks)
    {
        const WorldFixedStepSchedule::Clock::time_point startedAt{};
        WorldFixedStepSchedule schedule = CreateSchedule(10, 3, 100, startedAt);
        WorldFixedStepBatch firstBatch;
        WorldFixedStepBatch secondBatch;

        ASSERT_EQ(schedule.TryTakeDueTicks(startedAt + std::chrono::milliseconds{550}, &firstBatch),
                  WorldFixedStepTakeResult::Taken);
        EXPECT_EQ(firstBatch.firstServerTick, 100u);
        EXPECT_EQ(firstBatch.stepCount, 3u);
        EXPECT_TRUE(firstBatch.overrun);
        EXPECT_EQ(schedule.NextDeadline(), startedAt + std::chrono::milliseconds{400});

        ASSERT_EQ(schedule.TryTakeDueTicks(startedAt + std::chrono::milliseconds{550}, &secondBatch),
                  WorldFixedStepTakeResult::Taken);
        EXPECT_EQ(secondBatch.firstServerTick, 103u);
        EXPECT_EQ(secondBatch.stepCount, 2u);
        EXPECT_FALSE(secondBatch.overrun);
        EXPECT_EQ(schedule.NextDeadline(), startedAt + std::chrono::milliseconds{600});
    }
} // namespace psnr::world::tests
