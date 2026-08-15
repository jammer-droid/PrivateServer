#pragma once

#include "WorldEntityComponents.h"

#include <cstdint>

namespace psnr::world
{
    enum class WorldBodyTrailTrimResult : std::uint8_t
    {
        Trimmed = 0,
        WithinLength,
        InvalidInput,
        BufferInvariantViolation,
    };

    class WorldBodyTrailTrimmer final
    {
    public:
        [[nodiscard]] static WorldBodyTrailTrimResult Trim(float committedHeadX, float committedHeadY,
                                                           float nominalLength, BodyTrailComponent& bodyTrail) noexcept;
    };
} // namespace psnr::world
