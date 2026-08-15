#include "pch.h"

#include "WorldFixedStepSchedule.h"

#include <algorithm>
#include <limits>

namespace psnr::world
{
    WorldResult<WorldFixedStepSchedule> CreateWorldFixedStepSchedule(
        const WorldFixedStepScheduleConfig& config, const WorldFixedStepSchedule::Clock::time_point startedAt) noexcept
    {
        if (config.tickRateHz == 0 || config.maxCatchUpSteps == 0)
        {
            return WorldResult<WorldFixedStepSchedule>::Failure(WorldErrorCode::InvalidConfig);
        }

        const WorldFixedStepSchedule::Clock::duration oneSecond =
            std::chrono::duration_cast<WorldFixedStepSchedule::Clock::duration>(std::chrono::seconds{1});
        const WorldFixedStepSchedule::Clock::duration fixedStep = oneSecond / config.tickRateHz;
        if (fixedStep <= WorldFixedStepSchedule::Clock::duration::zero())
        {
            return WorldResult<WorldFixedStepSchedule>::Failure(WorldErrorCode::InvalidConfig);
        }

        WorldFixedStepSchedule schedule;
        schedule.fixedStep_ = fixedStep;
        schedule.nextDeadline_ = startedAt + fixedStep;
        schedule.nextServerTick_ = config.firstServerTick;
        schedule.maxCatchUpSteps_ = config.maxCatchUpSteps;
        schedule.valid_ = true;

        return WorldResult<WorldFixedStepSchedule>{schedule};
    }

    bool WorldFixedStepSchedule::IsValid() const noexcept
    {
        return valid_;
    }

    WorldFixedStepSchedule::Clock::duration WorldFixedStepSchedule::FixedStep() const noexcept
    {
        return fixedStep_;
    }

    WorldFixedStepSchedule::Clock::time_point WorldFixedStepSchedule::NextDeadline() const noexcept
    {
        return nextDeadline_;
    }

    std::uint64_t WorldFixedStepSchedule::NextServerTick() const noexcept
    {
        return nextServerTick_;
    }

    // 현재 시각을 기준으로 실행할 시점이 된 tick 을 계산하고, 그 tick 구간을 batch 로 출력
    WorldFixedStepTakeResult WorldFixedStepSchedule::TryTakeDueTicks(const Clock::time_point now,
                                                                     WorldFixedStepBatch* const outBatch) noexcept
    {
        if (outBatch == nullptr)
        {
            return WorldFixedStepTakeResult::InvalidArgument;
        }
        if (!IsValid())
        {
            return WorldFixedStepTakeResult::InvalidState;
        }

        if (now < nextDeadline_) // 아직 다음 tick 시간이 되지 않음
        {
            return WorldFixedStepTakeResult::NotDue;
        }

        const Clock::duration overdue = now - nextDeadline_;
        const std::uint64_t dueStepCount =
            static_cast<std::uint64_t>(overdue / fixedStep_) + 1; // nextDeadline 포함하기 위해 +1
        const std::uint64_t stepCount = std::min<std::uint64_t>(dueStepCount, maxCatchUpSteps_);
        constexpr std::uint64_t MaximumServerTick = std::numeric_limits<std::uint32_t>::max();
        if (nextServerTick_ > MaximumServerTick || stepCount - 1 > MaximumServerTick - nextServerTick_)
        {
            return WorldFixedStepTakeResult::ServerTickOverflow;
        }

        const WorldFixedStepBatch batch{
            static_cast<std::uint32_t>(nextServerTick_),
            static_cast<std::uint32_t>(stepCount),
            dueStepCount > stepCount,
        };

        nextServerTick_ += stepCount;
        nextDeadline_ += fixedStep_ * static_cast<Clock::duration::rep>(stepCount);
        *outBatch = batch;
        return WorldFixedStepTakeResult::Taken;
    }
} // namespace psnr::world
