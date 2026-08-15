#include "pch.h"

#include "WorldPlayerBody.h"

#include "WorldBodyTrailTrimmer.h"

#include <cmath>
#include <utility>

namespace psnr::world
{
    bool WorldPlayerBody::IsEnabled(const WorldPlayerBodyConfig& config) noexcept
    {
        return config.maxTrailSampleCount != 0;
    }

    bool WorldPlayerBody::IsValidConfig(const WorldPlayerBodyConfig& config) noexcept
    {
        return config.maxTrailSampleCount >= 2 && WorldGrowthSolver::IsValidConfig(config.growth);
    }

    WorldPlayerBodyUpdateResult WorldPlayerBody::Initialize(const WorldPlayerBodyConfig& config,
                                                            WorldEntityComponents* const components) noexcept
    {
        if (components == nullptr)
        {
            return WorldPlayerBodyUpdateResult::InvalidArgument;
        }
        if (!IsValidConfig(config))
        {
            return WorldPlayerBodyUpdateResult::InvalidConfig;
        }
        if (components->replicationMetadata.entityKind != WorldEntityKind::Player ||
            !std::isfinite(components->transform.positionX) || !std::isfinite(components->transform.positionY) ||
            !std::isfinite(components->transform.angleRadians))
        {
            return WorldPlayerBodyUpdateResult::InvalidEntityState;
        }

        WorldResult<WorldGrowthDimensions> dimensionsResult = WorldGrowthSolver::Solve(config.growth, 0);
        if (dimensionsResult.Failed())
        {
            return WorldPlayerBodyUpdateResult::GrowthSolveFailed;
        }
        const WorldGrowthDimensions dimensions = dimensionsResult.TakeValue();

        const float tailX =
            components->transform.positionX - std::cos(components->transform.angleRadians) * dimensions.nominalLength;
        const float tailY =
            components->transform.positionY - std::sin(components->transform.angleRadians) * dimensions.nominalLength;
        const float bodyRadius = dimensions.diameter * 0.5f;
        if (!std::isfinite(tailX) || !std::isfinite(tailY) || !std::isfinite(bodyRadius))
        {
            return WorldPlayerBodyUpdateResult::GrowthSolveFailed;
        }

        WorldResult<BodyTrailComponent> createResult = CreateBodyTrailComponent(config.maxTrailSampleCount);
        if (createResult.Failed())
        {
            return createResult.Error() == WorldErrorCode::AllocationFailed
                       ? WorldPlayerBodyUpdateResult::AllocationFailed
                       : WorldPlayerBodyUpdateResult::TrailUpdateFailed;
        }
        BodyTrailComponent initializedTrail = createResult.TakeValue();
        if (!initializedTrail.TryPushBack(
                BodyTrailSample{components->transform.positionX, components->transform.positionY}) ||
            !initializedTrail.TryPushBack(BodyTrailSample{tailX, tailY}))
        {
            return WorldPlayerBodyUpdateResult::TrailUpdateFailed;
        }

        components->bodyTrail = std::move(initializedTrail);
        components->replicationMetadata.primaryCircleRadius = bodyRadius;
        return WorldPlayerBodyUpdateResult::Updated;
    }

    WorldPlayerBodyUpdateResult WorldPlayerBody::Finalize(const WorldPlayerBodyConfig& config,
                                                          const std::uint32_t growthPoint,
                                                          WorldEntityComponents* const components) noexcept
    {
        if (components == nullptr)
        {
            return WorldPlayerBodyUpdateResult::InvalidArgument;
        }
        if (!IsValidConfig(config))
        {
            return WorldPlayerBodyUpdateResult::InvalidConfig;
        }
        if (components->replicationMetadata.entityKind != WorldEntityKind::Player ||
            !components->bodyTrail.IsInitialized() ||
            components->bodyTrail.MaxSampleCount() != config.maxTrailSampleCount || components->bodyTrail.Empty())
        {
            return WorldPlayerBodyUpdateResult::InvalidEntityState;
        }

        WorldResult<WorldGrowthDimensions> dimensionsResult = WorldGrowthSolver::Solve(config.growth, growthPoint);
        if (dimensionsResult.Failed())
        {
            return WorldPlayerBodyUpdateResult::GrowthSolveFailed;
        }
        const WorldGrowthDimensions dimensions = dimensionsResult.TakeValue();

        const WorldBodyTrailTrimResult trimResult =
            WorldBodyTrailTrimmer::Trim(components->transform.positionX, components->transform.positionY,
                                        dimensions.nominalLength, components->bodyTrail);
        if (trimResult != WorldBodyTrailTrimResult::Trimmed && trimResult != WorldBodyTrailTrimResult::WithinLength)
        {
            return WorldPlayerBodyUpdateResult::TrailUpdateFailed;
        }

        components->replicationMetadata.primaryCircleRadius = dimensions.diameter * 0.5f;
        return WorldPlayerBodyUpdateResult::Updated;
    }
} // namespace psnr::world
