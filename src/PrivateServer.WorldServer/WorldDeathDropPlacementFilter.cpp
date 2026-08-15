#include "pch.h"

#include "WorldDeathDropPlacementFilter.h"

#include <cmath>
#include <set>
#include <utility>

namespace psnr::world
{
    WorldResult<WorldDeathDropPlacementPlan> WorldDeathDropPlacementFilter::Filter(
        const WorldActiveArea& activeArea, const float resourceCircleRadius,
        const std::span<const WorldDeathDropAnchor> candidates, // 이번 death 로 새로 drop 하는 후보 좌표
        const WorldResourceRegistry& resourceRegistry,
        const std::span<const WorldResourcePosition> reservedPositions) noexcept
    {
        if (!activeArea.IsValid() || !std::isfinite(resourceCircleRadius) || resourceCircleRadius <= 0.0f)
        {
            return WorldResult<WorldDeathDropPlacementPlan>::Failure(WorldErrorCode::InvalidInput);
        }

        try
        {
            using PositionKey = std::pair<float, float>;
            std::set<PositionKey> acceptedPositionKeys;
            for (const WorldResourcePosition& reservedPosition : reservedPositions)
            {
                if (!std::isfinite(reservedPosition.positionX) || !std::isfinite(reservedPosition.positionY))
                {
                    return WorldResult<WorldDeathDropPlacementPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                acceptedPositionKeys.emplace(reservedPosition.positionX, reservedPosition.positionY);
            }

            WorldDeathDropPlacementPlan plan;
            plan.acceptedAnchors.reserve(candidates.size());
            for (const WorldDeathDropAnchor& candidate : candidates)
            {
                if (!std::isfinite(candidate.positionX) || !std::isfinite(candidate.positionY))
                {
                    return WorldResult<WorldDeathDropPlacementPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                if (!activeArea.ContainsCircleStrictly(candidate.positionX, candidate.positionY, resourceCircleRadius))
                {
                    ++plan.rejectedCount;
                    continue;
                }

                const PositionKey positionKey{candidate.positionX, candidate.positionY};
                if (resourceRegistry.ContainsExactPosition(candidate.positionX, candidate.positionY))
                {
                    ++plan.rejectedCount;
                    continue;
                }
                const std::pair<std::set<PositionKey>::iterator, bool> insertionResult =
                    acceptedPositionKeys.insert(positionKey);
                if (!insertionResult.second) // 이미 존재하는 경우에는 reject count 올리고 continue
                {
                    ++plan.rejectedCount;
                    continue;
                }

                plan.acceptedAnchors.push_back(candidate);
            }

            return WorldResult<WorldDeathDropPlacementPlan>(std::move(plan));
        }
        catch (...)
        {
            return WorldResult<WorldDeathDropPlacementPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
