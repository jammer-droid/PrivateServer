#pragma once

#include "BenchmarkConfig.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace psnr::benchmark
{
    inline constexpr std::uint32_t BenchmarkEventPollIntervalMilliseconds = 1;
    inline constexpr std::size_t BenchmarkActiveDrainProbeBudget = 4;

    enum class BenchmarkWorkloadPhase : std::uint8_t
    {
        Connect,
        Warmup,
        Measurement,
        Drain,
    };

    struct BenchmarkWorkloadTimelineSnapshot final
    {
        std::chrono::steady_clock::time_point workloadBegin;
        std::chrono::steady_clock::time_point measurementBegin;
        std::chrono::steady_clock::time_point measurementEnd;
    };

    class BenchmarkWorkloadTimeline final
    {
    public:
        void Publish(const BenchmarkWorkloadTimelineSnapshot& timeline);
        [[nodiscard]] bool TryRead(BenchmarkWorkloadTimelineSnapshot* outTimeline) const;

    private:
        mutable std::mutex mutex_;
        BenchmarkWorkloadTimelineSnapshot timeline_;
        bool published_ = false;
    };

    struct BenchmarkClientWorkloadResult final
    {
        std::uint64_t accepted = 0;
        std::uint64_t completed = 0;
        std::uint64_t sendRejected = 0;
        std::uint64_t timeout = 0;
        std::uint64_t unexpectedDisconnect = 0;
        std::uint64_t missedSchedule = 0;
        std::vector<std::int64_t> applicationObservedRttNanoseconds;
        std::vector<std::int64_t> serverProcessingDurationNanoseconds;
        std::vector<std::int64_t> schedulerLagNanoseconds;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkClientWorkload final
    {
    public:
        [[nodiscard]] static BenchmarkClientWorkloadResult Run(const BenchmarkConfigV1& config,
                                                               BenchmarkWorkloadTimeline& timeline);
    };
} // namespace psnr::benchmark
