#include "pch.h"

#include "WorldBodyTrailSampler.h"

#include <cmath>

namespace psnr::world
{
    bool WorldBodyTrailSampler::IsValidConfig(const WorldBodyTrailSampleConfig& config) noexcept
    {
        return config.sampleIntervalTicks > 0 && config.maxSampleCount > 0;
    }

    WorldBodyTrailSampleResult WorldBodyTrailSampler::Sample(const WorldBodyTrailSampleConfig& config,
                                                             const std::uint32_t serverTick, const float committedHeadX,
                                                             const float committedHeadY,
                                                             BodyTrailComponent& bodyTrail) noexcept
    {
        if (!IsValidConfig(config))
        {
            return WorldBodyTrailSampleResult::InvalidConfig;
        }
        if (!std::isfinite(committedHeadX) || !std::isfinite(committedHeadY))
        {
            return WorldBodyTrailSampleResult::InvalidPosition;
        }
        if (serverTick % config.sampleIntervalTicks != 0)
        {
            return WorldBodyTrailSampleResult::NotDue;
        }

        if (!bodyTrail.IsInitialized())
        {
            WorldResult<BodyTrailComponent> createResult = CreateBodyTrailComponent(config.maxSampleCount);
            if (createResult.Failed())
            {
                return createResult.Error() == WorldErrorCode::AllocationFailed
                           ? WorldBodyTrailSampleResult::AllocationFailed
                           : WorldBodyTrailSampleResult::BufferInvariantViolation;
            }
            bodyTrail = createResult.TakeValue();
        }
        else if (bodyTrail.MaxSampleCount() != config.maxSampleCount)
        {
            return WorldBodyTrailSampleResult::BufferInvariantViolation;
        }

        return bodyTrail.TryPushFront(BodyTrailSample{committedHeadX, committedHeadY})
                   ? WorldBodyTrailSampleResult::Sampled
                   : WorldBodyTrailSampleResult::CapacityExceeded;
    }
} // namespace psnr::world
