#include "pch.h"

#include "WorldPlayerSpawnPlanner.h"

#include "WorldDeterministicSampler.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <new>
#include <numbers>
#include <utility>

namespace psnr::world
{
    namespace
    {
        constexpr std::uint32_t MaximumCandidateCount = 4;
        constexpr float BoundaryClearance = 0.001f;
        [[nodiscard]] WorldEntityComponents MakeCandidateComponents(const WorldPlayerSpawnPlannerConfig& config,
                                                                    const std::uint32_t playerId, const float positionX,
                                                                    const float positionY, const float headingRadians)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{positionX, positionY, headingRadians};
            components.motion = MotionComponent{};
            components.movementCapability = MovementCapabilityComponent{config.playerMaxMoveSpeed};
            components.replicationMetadata = ReplicationMetadataComponent{
                WorldEntityKind::Player,
                config.playerArchetypeId,
                WorldShapeKind::Circle,
                0.0f,
            };
            components.playerControl = PlayerControlComponent{playerId};
            return components;
        }
    } // namespace

    WorldResult<WorldPlayerSpawnCandidate> WorldPlayerSpawnPlanner::Plan(const WorldPlayerSpawnPlannerConfig& config,
                                                                         const std::uint32_t serverTick,
                                                                         const std::uint32_t playerId,
                                                                         const std::uint32_t candidateOrdinal) noexcept
    {
        if (playerId == 0)
        {
            return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::InvalidArgument);
        }
        if (!WorldPhysicsArenaBounds::IsValid(config.arenaBounds) || config.maxCandidatesPerTick == 0 ||
            config.maxCandidatesPerTick > MaximumCandidateCount || config.playerArchetypeId == 0 ||
            !std::isfinite(config.playerMaxMoveSpeed) || config.playerMaxMoveSpeed <= 0.0f ||
            !WorldPlayerBody::IsValidConfig(config.playerBody))
        {
            return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::InvalidConfig);
        }
        if (candidateOrdinal >= config.maxCandidatesPerTick)
        {
            return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::InvalidArgument);
        }

        WorldResult<WorldGrowthDimensions> initialDimensionsResult =
            WorldGrowthSolver::Solve(config.playerBody.growth, 0);
        if (initialDimensionsResult.Failed())
        {
            return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::InvalidConfig);
        }
        const WorldGrowthDimensions initialDimensions = initialDimensionsResult.TakeValue();
        const float bodyRadius = initialDimensions.diameter * 0.5f;
        const float margin = initialDimensions.nominalLength + bodyRadius + BoundaryClearance;
        const float minimumX = config.arenaBounds.minimumX + margin;
        const float minimumY = config.arenaBounds.minimumY + margin;
        const float maximumX = config.arenaBounds.maximumX - margin;
        const float maximumY = config.arenaBounds.maximumY - margin;
        if (!std::isfinite(margin) || minimumX > maximumX || minimumY > maximumY)
        {
            return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::InvalidConfig);
        }

        try
        {
            WorldResult<float> positionXUnitResult = WorldDeterministicSampler::SampleUnit(
                WorldDeterministicSampleDomain::PlayerSpawn, serverTick, playerId, candidateOrdinal, 0);
            WorldResult<float> positionYUnitResult = WorldDeterministicSampler::SampleUnit(
                WorldDeterministicSampleDomain::PlayerSpawn, serverTick, playerId, candidateOrdinal, 1);
            WorldResult<float> headingUnitResult = WorldDeterministicSampler::SampleUnit(
                WorldDeterministicSampleDomain::PlayerSpawn, serverTick, playerId, candidateOrdinal, 2);
            if (positionXUnitResult.Failed() || positionYUnitResult.Failed() || headingUnitResult.Failed())
            {
                return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::InvalidConfig);
            }
            const float positionXUnit = positionXUnitResult.TakeValue();
            const float positionYUnit = positionYUnitResult.TakeValue();
            const float headingUnit = headingUnitResult.TakeValue();
            const float positionX = minimumX + (maximumX - minimumX) * positionXUnit;
            const float positionY = minimumY + (maximumY - minimumY) * positionYUnit;

            // [-pi, pi)
            const float headingRadians = -std::numbers::pi_v<float> + 2.0f * std::numbers::pi_v<float> * headingUnit;

            WorldEntityComponents components =
                MakeCandidateComponents(config, playerId, positionX, positionY, headingRadians);
            const WorldPlayerBodyUpdateResult bodyResult = WorldPlayerBody::Initialize(config.playerBody, &components);
            if (bodyResult == WorldPlayerBodyUpdateResult::AllocationFailed)
            {
                return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::AllocationFailed);
            }
            if (bodyResult != WorldPlayerBodyUpdateResult::Updated)
            {
                return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::DependencyFailure);
            }

            WorldPlayerSpawnBounds candidateBounds;
            if (!TryProjectBounds(components, &candidateBounds) ||
                candidateBounds.minX <= config.arenaBounds.minimumX ||
                candidateBounds.minY <= config.arenaBounds.minimumY ||
                candidateBounds.maxX >= config.arenaBounds.maximumX ||
                candidateBounds.maxY >= config.arenaBounds.maximumY)
            {
                return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::DependencyFailure);
            }

            return WorldResult<WorldPlayerSpawnCandidate>(
                WorldPlayerSpawnCandidate{playerId, candidateOrdinal, std::move(components), candidateBounds});
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldPlayerSpawnCandidate>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    bool WorldPlayerSpawnPlanner::TryProjectBounds(const WorldEntityComponents& components,
                                                   WorldPlayerSpawnBounds* const outBounds) noexcept
    {
        if (outBounds == nullptr || components.replicationMetadata.entityKind != WorldEntityKind::Player ||
            !std::isfinite(components.transform.positionX) || !std::isfinite(components.transform.positionY) ||
            !std::isfinite(components.replicationMetadata.primaryCircleRadius) ||
            components.replicationMetadata.primaryCircleRadius <= 0.0f || !components.bodyTrail.IsInitialized() ||
            components.bodyTrail.Empty())
        {
            return false;
        }

        float minimumX = components.transform.positionX;
        float minimumY = components.transform.positionY;
        float maximumX = components.transform.positionX;
        float maximumY = components.transform.positionY;
        for (std::size_t index = 0; index < components.bodyTrail.SampleCount(); ++index)
        {
            BodyTrailSample sample;
            if (!components.bodyTrail.TryRead(index, &sample) || !std::isfinite(sample.positionX) ||
                !std::isfinite(sample.positionY))
            {
                return false;
            }
            minimumX = std::min(minimumX, sample.positionX);
            minimumY = std::min(minimumY, sample.positionY);
            maximumX = std::max(maximumX, sample.positionX);
            maximumY = std::max(maximumY, sample.positionY);
        }

        const float radius = components.replicationMetadata.primaryCircleRadius;
        const WorldPlayerSpawnBounds projected{minimumX - radius, minimumY - radius, maximumX + radius,
                                               maximumY + radius};
        if (!WorldPlayerSpawnBounds::IsValid(projected))
        {
            return false;
        }

        *outBounds = projected;
        return true;
    }
} // namespace psnr::world
