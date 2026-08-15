#include "pch.h"

#include "WorldResourceSpawnPlanner.h"

#include "WorldDeathDropAnchorPlanner.h"
#include "WorldDeathDropPlacementFilter.h"
#include "WorldDeterministicSampler.h"

#include <cmath>
#include <new>
#include <numbers>
#include <utility>

namespace psnr::world
{
    namespace
    {
        constexpr std::uint32_t MaximumAmbientCandidateCount = 8;

        struct AmbientSpawnCandidate final
        {
            std::uint32_t ambientSlotId = 0;
            float positionX = 0.0f;
            float positionY = 0.0f;
        };

        [[nodiscard]] bool IsReservedPosition(const std::span<const WorldResourcePosition> reservedPositions,
                                              const float positionX, const float positionY) noexcept
        {
            for (const WorldResourcePosition& position : reservedPositions)
            {
                if (position.positionX == positionX && position.positionY == positionY)
                {
                    return true;
                }
            }
            return false;
        }
        [[nodiscard]] WorldResult<AmbientSpawnCandidate> PlanAmbientCandidate(
            const WorldActiveArea& activeArea, const float resourceCircleRadius, const std::uint32_t serverTick,
            const std::uint32_t ambientSlotId, const std::uint32_t candidateOrdinal) noexcept
        {
            if (ambientSlotId == 0)
            {
                return WorldResult<AmbientSpawnCandidate>::Failure(WorldErrorCode::InvalidArgument);
            }
            if (!activeArea.IsValid() || !std::isfinite(resourceCircleRadius) || resourceCircleRadius <= 0.0f)
            {
                return WorldResult<AmbientSpawnCandidate>::Failure(WorldErrorCode::InvalidInput);
            }

            // resource 크기만큼 ActiveArea 경계를 안쪽으로 줄인 영역
            const float erodedRadius = activeArea.radius - resourceCircleRadius;
            if (!std::isfinite(erodedRadius) || erodedRadius <= 0.0f)
            {
                return WorldResult<AmbientSpawnCandidate>::Failure(WorldErrorCode::InvalidInput);
            }
            // float 으로 표현 가능한 값 중 erodedRadius 보다 바로 작은 값 반환
            const float samplingRadius = std::nextafter(erodedRadius, 0.0f);
            if (!std::isfinite(samplingRadius) || samplingRadius <= 0.0f)
            {
                return WorldResult<AmbientSpawnCandidate>::Failure(WorldErrorCode::InvalidInput);
            }

            // Sample Channel
            // channel 0: radial Unit
            // channel 1: angular Unit
            WorldResult<float> radialUnitResult = WorldDeterministicSampler::SampleUnit(
                WorldDeterministicSampleDomain::AmbientResourceSpawn, serverTick, ambientSlotId, candidateOrdinal, 0);
            WorldResult<float> angularUnitResult = WorldDeterministicSampler::SampleUnit(
                WorldDeterministicSampleDomain::AmbientResourceSpawn, serverTick, ambientSlotId, candidateOrdinal, 1);
            if (radialUnitResult.Failed() || angularUnitResult.Failed())
            {
                return WorldResult<AmbientSpawnCandidate>::Failure(WorldErrorCode::InvalidInput);
            }
            const float radialUnit = radialUnitResult.TakeValue();                     // 중심으로부터의 거리
            const float angularUnit = angularUnitResult.TakeValue();                   // 방향 각도
            const float distance = std::sqrt(radialUnit) * samplingRadius;             // [0, samplingRad)
            const float angleRadians = 2.0f * std::numbers::pi_v<float> * angularUnit; // [0, 2pi)
            const AmbientSpawnCandidate candidate{
                ambientSlotId,
                activeArea.centerX + std::cos(angleRadians) * distance,
                activeArea.centerY + std::sin(angleRadians) * distance,
            };
            if (!activeArea.ContainsCircleStrictly(candidate.positionX, candidate.positionY, resourceCircleRadius))
            {
                return WorldResult<AmbientSpawnCandidate>::Failure(WorldErrorCode::InvalidInput);
            }

            return WorldResult<AmbientSpawnCandidate>(candidate);
        }
    } // namespace

    WorldResult<WorldResourceSpawnPlan> WorldResourceSpawnPlanner::PlanAmbient(
        const WorldActiveArea& activeArea, const float resourceCircleRadius, const std::uint32_t serverTick,
        const std::uint32_t ambientSlotId, const WorldResourceRegistry& resourceRegistry,
        const std::span<const WorldResourcePosition> reservedPositions) noexcept
    {
        if (ambientSlotId == 0)
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::InvalidArgument);
        }
        for (const WorldResourcePosition& position : reservedPositions)
        {
            if (!std::isfinite(position.positionX) || !std::isfinite(position.positionY))
            {
                return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::InvalidInput);
            }
        }

        try
        {
            for (std::uint32_t candidateOrdinal = 0; candidateOrdinal < MaximumAmbientCandidateCount;
                 ++candidateOrdinal)
            {
                WorldResult<AmbientSpawnCandidate> candidateResult =
                    PlanAmbientCandidate(activeArea, resourceCircleRadius, serverTick, ambientSlotId, candidateOrdinal);
                if (candidateResult.Failed())
                {
                    return WorldResult<WorldResourceSpawnPlan>::Failure(candidateResult.Error());
                }
                const AmbientSpawnCandidate candidate = candidateResult.TakeValue();
                if (resourceRegistry.ContainsExactPosition(candidate.positionX, candidate.positionY) ||
                    IsReservedPosition(reservedPositions, candidate.positionX, candidate.positionY))
                {
                    continue;
                }

                WorldResourceSpawnPlan plan;
                plan.requests.reserve(1);
                plan.requests.push_back(WorldResourceSpawnRequest{WorldResourceOrigin::Ambient,
                                                                  ambientSlotId,
                                                                  {},
                                                                  candidateOrdinal,
                                                                  candidate.positionX,
                                                                  candidate.positionY});
                return WorldResult<WorldResourceSpawnPlan>(std::move(plan));
            }
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::CapacityExceeded);
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<WorldResourceSpawnPlan> WorldResourceSpawnPlanner::PlanDeathDrop(
        const WorldActiveArea& activeArea, const float resourceCircleRadius, const WorldEntityKey sourceEntityKey,
        const float headPositionX, const float headPositionY, const BodyTrailComponent& bodyTrail,
        const std::uint32_t growthPoint, const WorldResourceRegistry& resourceRegistry,
        const std::span<const WorldResourcePosition> reservedPositions) noexcept
    {
        if (!sourceEntityKey.IsValid())
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::InvalidArgument);
        }

        WorldResult<std::vector<WorldDeathDropAnchor>> anchorResult =
            WorldDeathDropAnchorPlanner::Plan(headPositionX, headPositionY, bodyTrail, growthPoint);
        if (anchorResult.Failed() && anchorResult.Error() == WorldErrorCode::AllocationFailed)
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
        if (anchorResult.Failed())
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::InvalidInput);
        }
        std::vector<WorldDeathDropAnchor> anchors = anchorResult.TakeValue();

        WorldResult<WorldDeathDropPlacementPlan> placementResult = WorldDeathDropPlacementFilter::Filter(
            activeArea, resourceCircleRadius, anchors, resourceRegistry, reservedPositions);
        if (placementResult.Failed() && placementResult.Error() == WorldErrorCode::AllocationFailed)
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
        if (placementResult.Failed())
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::InvalidInput);
        }
        const WorldDeathDropPlacementPlan placementPlan = placementResult.TakeValue();

        try
        {
            WorldResourceSpawnPlan plan;
            plan.requests.reserve(placementPlan.acceptedAnchors.size());
            plan.rejectedCount = placementPlan.rejectedCount;
            for (const WorldDeathDropAnchor& anchor : placementPlan.acceptedAnchors)
            {
                plan.requests.push_back(WorldResourceSpawnRequest{WorldResourceOrigin::DeathDrop, 0, sourceEntityKey,
                                                                  anchor.dropOrdinal, anchor.positionX,
                                                                  anchor.positionY});
            }
            return WorldResult<WorldResourceSpawnPlan>(std::move(plan));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldResourceSpawnPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
