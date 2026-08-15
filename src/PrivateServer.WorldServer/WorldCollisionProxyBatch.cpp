#include "pch.h"

#include "WorldCollisionProxyBatch.h"

#include <cmath>

namespace psnr::world
{
    bool WorldPlayerSpawnBounds::IsValid(const WorldPlayerSpawnBounds& bounds) noexcept
    {
        return std::isfinite(bounds.minX) && std::isfinite(bounds.minY) && std::isfinite(bounds.maxX) &&
               std::isfinite(bounds.maxY) && bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY;
    }

    void WorldCollisionProxyBatch::Clear() noexcept
    {
        proxies_.clear();
    }

    WorldCollisionProxyAppendResult WorldCollisionProxyBatch::AppendPlayer(
        const WorldEntityKey ownerKey, const WorldEntityComponents& components) noexcept
    {
        const ReplicationMetadataComponent& metadata = components.replicationMetadata;
        if (!ownerKey.IsValid() || metadata.entityKind != WorldEntityKind::Player ||
            metadata.primaryShapeKind != WorldShapeKind::Circle || !std::isfinite(components.transform.positionX) ||
            !std::isfinite(components.transform.positionY) || !std::isfinite(metadata.primaryCircleRadius) ||
            metadata.primaryCircleRadius <= 0.0f || !components.bodyTrail.IsInitialized() ||
            components.bodyTrail.Empty())
        {
            return WorldCollisionProxyAppendResult::InvalidEntityState;
        }

        const std::size_t bodyProxyCount = components.bodyTrail.SampleCount();
        const std::size_t maxProxyCount = proxies_.max_size();
        if (bodyProxyCount >= maxProxyCount || proxies_.size() > maxProxyCount - bodyProxyCount - 1)
        {
            return WorldCollisionProxyAppendResult::CapacityExceeded;
        }

        for (std::size_t index = 0; index < bodyProxyCount; ++index)
        {
            BodyTrailSample sample;
            if (!components.bodyTrail.TryRead(index, &sample) || !std::isfinite(sample.positionX) ||
                !std::isfinite(sample.positionY))
            {
                return WorldCollisionProxyAppendResult::InvalidEntityState;
            }
        }

        try
        {
            proxies_.reserve(proxies_.size() + bodyProxyCount + 1);
        }
        catch (...)
        {
            return WorldCollisionProxyAppendResult::AllocationFailed;
        }

        // head proxy 추가
        proxies_.push_back(WorldCollisionProxy{
            ownerKey,
            WorldCollisionProxyRole::PlayerHead,
            0,
            components.transform.positionX,
            components.transform.positionY,
            components.transform.positionX,
            components.transform.positionY,
            metadata.primaryCircleRadius,
        });

        // body proxy 추가
        float segmentStartX = components.transform.positionX;
        float segmentStartY = components.transform.positionY;
        for (std::size_t index = 0; index < bodyProxyCount; ++index)
        {
            // Trim이 완료된 BodyTrail 을 사용해야 한다.
            BodyTrailSample segmentEnd;
            const bool readSucceeded = components.bodyTrail.TryRead(index, &segmentEnd);
            static_cast<void>(readSucceeded);

            proxies_.push_back(WorldCollisionProxy{
                ownerKey,
                WorldCollisionProxyRole::PlayerBody,
                static_cast<std::uint32_t>(index),
                segmentStartX,
                segmentStartY,
                segmentEnd.positionX,
                segmentEnd.positionY,
                metadata.primaryCircleRadius,
            });
            segmentStartX = segmentEnd.positionX;
            segmentStartY = segmentEnd.positionY;
        }

        return WorldCollisionProxyAppendResult::Appended;
    }

    std::span<const WorldCollisionProxy> WorldCollisionProxyBatch::Proxies() const noexcept
    {
        return std::span<const WorldCollisionProxy>(proxies_.data(), proxies_.size());
    }

    std::size_t WorldCollisionProxyBatch::Size() const noexcept
    {
        return proxies_.size();
    }

    std::size_t WorldCollisionProxyBatch::Capacity() const noexcept
    {
        return proxies_.capacity();
    }
} // namespace psnr::world
