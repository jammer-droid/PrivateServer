#include "pch.h"

#include "WorldActiveAreaSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psnr::world
{
    bool WorldActiveArea::IsValid() const noexcept
    {
        const float expectedRadius = referenceRadius * ratio;
        return std::isfinite(centerX) && std::isfinite(centerY) && std::isfinite(referenceRadius) &&
               referenceRadius > 0.0f && std::isfinite(ratio) && ratio > 0.0f && ratio <= 1.0f &&
               std::isfinite(radius) && radius > 0.0f && std::isfinite(expectedRadius) && radius == expectedRadius;
    }

    bool WorldActiveArea::ContainsCircleStrictly(const float circleCenterX, const float circleCenterY,
                                                 const float circleRadius) const noexcept
    {
        if (!IsValid() || !std::isfinite(circleCenterX) || !std::isfinite(circleCenterY) ||
            !std::isfinite(circleRadius) || circleRadius <= 0.0f)
        {
            return false;
        }

        const float distance = std::hypot(circleCenterX - centerX, circleCenterY - centerY);
        return distance + circleRadius < radius;
    }

    bool WorldActiveAreaSolver::IsValidConfig(const WorldActiveAreaConfig& config) noexcept
    {
        if (!WorldPhysicsArenaBounds::IsValid(config.mapBounds) || config.roundDurationTicks == 0 ||
            !std::isfinite(config.startRatio) || !std::isfinite(config.endRatio) || config.endRatio <= 0.0f ||
            config.endRatio > config.startRatio || config.startRatio > 1.0f)
        {
            return false;
        }

        const float mapWidth = config.mapBounds.maximumX - config.mapBounds.minimumX;
        const float mapHeight = config.mapBounds.maximumY - config.mapBounds.minimumY;
        const float centerX = config.mapBounds.minimumX + mapWidth * 0.5f;
        const float centerY = config.mapBounds.minimumY + mapHeight * 0.5f;
        const float referenceRadius = std::min(mapWidth, mapHeight) * 0.5f;
        return std::isfinite(mapWidth) && std::isfinite(mapHeight) && std::isfinite(centerX) &&
               std::isfinite(centerY) && std::isfinite(referenceRadius) && referenceRadius > 0.0f;
    }

    WorldResult<WorldActiveArea> WorldActiveAreaSolver::Solve(const WorldActiveAreaConfig& config,
                                                              const std::uint32_t roundStartTick,
                                                              const std::uint32_t serverTick) noexcept
    {
        if (!IsValidConfig(config))
        {
            return WorldResult<WorldActiveArea>::Failure(WorldErrorCode::InvalidConfig);
        }
        if (roundStartTick > std::numeric_limits<std::uint32_t>::max() - config.roundDurationTicks)
        {
            return WorldResult<WorldActiveArea>::Failure(WorldErrorCode::InvalidInput);
        }

        const std::uint32_t elapsedTicks =
            serverTick <= roundStartTick ? 0 : std::min(serverTick - roundStartTick, config.roundDurationTicks);
        const float progress = static_cast<float>(elapsedTicks) / static_cast<float>(config.roundDurationTicks);
        const float ratio = config.startRatio + (config.endRatio - config.startRatio) * progress;
        const float mapWidth = config.mapBounds.maximumX - config.mapBounds.minimumX;
        const float mapHeight = config.mapBounds.maximumY - config.mapBounds.minimumY;
        const float referenceRadius = std::min(mapWidth, mapHeight) * 0.5f;
        const WorldActiveArea solved{
            config.mapBounds.minimumX + mapWidth * 0.5f,
            config.mapBounds.minimumY + mapHeight * 0.5f,
            referenceRadius,
            ratio,
            referenceRadius * ratio,
        };
        if (!solved.IsValid())
        {
            return WorldResult<WorldActiveArea>::Failure(WorldErrorCode::InvalidConfig);
        }

        return WorldResult<WorldActiveArea>(solved);
    }
} // namespace psnr::world
