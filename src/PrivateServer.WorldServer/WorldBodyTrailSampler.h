#pragma once

#include "WorldEntityComponents.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldBodyTrailSampleConfig final
    {
        std::uint32_t sampleIntervalTicks = 0;
        std::uint32_t maxSampleCount = 0;

        [[nodiscard]] friend bool operator==(const WorldBodyTrailSampleConfig& left,
                                             const WorldBodyTrailSampleConfig& right) noexcept = default;
    };

    enum class WorldBodyTrailSampleResult : std::uint8_t
    {
        Sampled = 0,
        NotDue,
        InvalidConfig,
        InvalidPosition,
        AllocationFailed,
        CapacityExceeded,
        BufferInvariantViolation,
    };

    class WorldBodyTrailSampler final
    {
    public:
        [[nodiscard]] static bool IsValidConfig(const WorldBodyTrailSampleConfig& config) noexcept;
        [[nodiscard]] static WorldBodyTrailSampleResult Sample(const WorldBodyTrailSampleConfig& config,
                                                               std::uint32_t serverTick, float committedHeadX,
                                                               float committedHeadY,
                                                               BodyTrailComponent& bodyTrail) noexcept;
    };
} // namespace psnr::world
