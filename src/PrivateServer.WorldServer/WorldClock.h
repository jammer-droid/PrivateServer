#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

namespace psnr::world
{
    using WorldClock = std::chrono::steady_clock;

    class WorldSteadyClockSource final
    {
    public:
        [[nodiscard]] WorldClock::time_point Now() const noexcept
        {
            return WorldClock::now();
        }

        void WaitUntil(const WorldClock::time_point deadline) const
        {
            std::this_thread::sleep_until(deadline);
        }
    };

    [[nodiscard]] inline std::uint32_t CalculateWorldWaitTimeoutMilliseconds(
        const WorldClock::duration remainingTime) noexcept
    {
        if (remainingTime <= WorldClock::duration::zero())
        {
            return 0;
        }

        const std::chrono::milliseconds wholeMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(remainingTime);

        // milliseconds 를 uint64_t 로 변환하면 소수점 ms 가 버려진다.
        // 정수부가 remainingTime 보다 작으면(=버려진 ms가 있으면) 대기 시간에 + 1ms 올림 처리
        std::uint64_t waitTimeoutMilliseconds = static_cast<std::uint64_t>(wholeMilliseconds.count());
        if (std::chrono::duration_cast<WorldClock::duration>(wholeMilliseconds) < remainingTime)
        {
            ++waitTimeoutMilliseconds; // ms 올림 처리(남은 ms 소수점 부분 있으면)
        }

        constexpr std::uint64_t MaximumTimeoutMilliseconds = std::numeric_limits<std::uint32_t>::max();
        if (waitTimeoutMilliseconds > MaximumTimeoutMilliseconds)
        {
            waitTimeoutMilliseconds = MaximumTimeoutMilliseconds;
        }
        return static_cast<std::uint32_t>(waitTimeoutMilliseconds);
    }
} // namespace psnr::world
