#pragma once

#include "WorldClock.h"
#include "WorldResult.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldFixedStepScheduleConfig final
    {
        std::uint32_t tickRateHz = 0;      // 1초에 몇 번 world tick 실행할지 빈도
        std::uint32_t maxCatchUpSteps = 0; // 서버가 밀렸을 때, 한 번에 따라잡을 수 있는 최대 tick 수
        std::uint32_t firstServerTick = 0; // 스케줄러에서 처음 실행할 server tick
    };

    struct WorldFixedStepBatch final // 이번에 실행해야 할 tick 범위 표시
    {
        std::uint32_t firstServerTick = 0;
        std::uint32_t stepCount = 0; // firstServerTick 을 기준으로 stepCount 만큼 tick 실행
        bool overrun = false;        // 아직 처리하지 못한 밀린 tick 존재 여부
    };

    enum class WorldFixedStepTakeResult : std::uint8_t
    {
        Taken = 0,
        NotDue,
        InvalidArgument,
        InvalidState,
        ServerTickOverflow,
    };

    // steady_clock deadline을 이전 deadline 기준으로 전진시켜 fixed-step tick 순서를 만든다.
    // 지연된 tick은 건너뛰지 않으며 한 번에 maxCatchUpSteps까지만 caller에게 넘긴다.
    class WorldFixedStepSchedule final
    {
    public:
        using Clock = WorldClock;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] Clock::duration FixedStep() const noexcept;
        [[nodiscard]] Clock::time_point NextDeadline() const noexcept;
        [[nodiscard]] std::uint64_t NextServerTick() const noexcept;

        [[nodiscard]] WorldFixedStepTakeResult TryTakeDueTicks(Clock::time_point now,
                                                               WorldFixedStepBatch* outBatch) noexcept;

    private:
        friend WorldResult<WorldFixedStepSchedule> CreateWorldFixedStepSchedule(
            const WorldFixedStepScheduleConfig& config, Clock::time_point startedAt) noexcept;

        Clock::duration fixedStep_{};
        Clock::time_point nextDeadline_{};
        std::uint64_t nextServerTick_ = 0;
        std::uint32_t maxCatchUpSteps_ = 0;
        bool valid_ = false;
    };

    [[nodiscard]] WorldResult<WorldFixedStepSchedule> CreateWorldFixedStepSchedule(
        const WorldFixedStepScheduleConfig& config, WorldFixedStepSchedule::Clock::time_point startedAt) noexcept;
} // namespace psnr::world
