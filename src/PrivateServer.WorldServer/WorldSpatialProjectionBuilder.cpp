#include "pch.h"

#include "WorldSpatialProjectionBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] bool TryCalculateVisibilityRadius(const WorldEntityComponents& components,
                                                        float* const outRadius) noexcept
        {
            const ReplicationMetadataComponent& metadata = components.replicationMetadata;
            if (metadata.entityKind != WorldEntityKind::Player)
            {
                *outRadius = metadata.primaryCircleRadius;
                return true;
            }
            if (!components.bodyTrail.IsInitialized() || components.bodyTrail.Empty())
            {
                *outRadius = metadata.primaryCircleRadius;
                return true;
            }

            // sample point 사이의 가장 긴 거리를 계산해 AOI 판단에 사용할 반지름으로 사용
            double maximumDistanceSquared = 0.0;
            for (std::size_t index = 0; index < components.bodyTrail.SampleCount(); ++index)
            {
                BodyTrailSample sample;
                if (!components.bodyTrail.TryRead(index, &sample) || !std::isfinite(sample.positionX) ||
                    !std::isfinite(sample.positionY))
                {
                    return false;
                }

                const double differenceX = static_cast<double>(sample.positionX) - components.transform.positionX;
                const double differenceY = static_cast<double>(sample.positionY) - components.transform.positionY;
                const double distanceSquared = differenceX * differenceX + differenceY * differenceY;
                if (!std::isfinite(distanceSquared))
                {
                    return false;
                }
                maximumDistanceSquared = (std::max)(maximumDistanceSquared, distanceSquared);
            }

            const double radius = std::sqrt(maximumDistanceSquared) + metadata.primaryCircleRadius;
            if (!std::isfinite(radius) || radius > (std::numeric_limits<float>::max)())
            {
                return false;
            }
            *outRadius = static_cast<float>(radius);
            return true;
        }
    } // namespace

    WorldResult<std::vector<WorldSpatialProxy>> WorldSpatialProjectionBuilder::Build(
        const WorldEntityManager& entityManager) noexcept
    {
        try
        {
            const std::span<const EntityHandle> activeHandles = entityManager.ActiveHandles();
            std::vector<WorldSpatialProxy> proxies;
            proxies.reserve(activeHandles.size());

            for (const EntityHandle handle : activeHandles)
            {
                WorldEntityKey entityKey;
                const WorldEntityComponents* components = nullptr;
                if (!entityManager.TryFindKey(handle, &entityKey) ||
                    !entityManager.TryReadComponentsView(handle, &components))
                {
                    return WorldResult<std::vector<WorldSpatialProxy>>::Failure(WorldErrorCode::InvalidState);
                }

                const ReplicationMetadataComponent& metadata = components->replicationMetadata;
                if (metadata.primaryShapeKind != WorldShapeKind::Circle ||
                    !std::isfinite(metadata.primaryCircleRadius) || metadata.primaryCircleRadius <= 0.0f)
                {
                    return WorldResult<std::vector<WorldSpatialProxy>>::Failure(WorldErrorCode::InvalidState);
                }

                float visibilityRadius = 0.0f;
                if (!TryCalculateVisibilityRadius(*components, &visibilityRadius))
                {
                    return WorldResult<std::vector<WorldSpatialProxy>>::Failure(WorldErrorCode::InvalidState);
                }

                const WorldSpatialProxy proxy{
                    entityKey,
                    metadata.entityKind,
                    components->transform.positionX,
                    components->transform.positionY,
                    visibilityRadius,
                };
                if (!IsValid(proxy))
                {
                    return WorldResult<std::vector<WorldSpatialProxy>>::Failure(WorldErrorCode::InvalidState);
                }

                proxies.push_back(proxy);
            }

            std::sort(proxies.begin(), proxies.end(), WorldSpatialProxyEntityKeyLess{});
            return WorldResult<std::vector<WorldSpatialProxy>>(std::move(proxies));
        }
        catch (...)
        {
            return WorldResult<std::vector<WorldSpatialProxy>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
