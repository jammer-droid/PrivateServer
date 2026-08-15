#pragma once

#include "WorldEntityComponents.h"
#include "WorldEntityIdentity.h"

namespace psnr::world
{
    // Spatial Index 와 AOI 계산에 사용하는 설정값
    struct WorldSpatialConfig final
    {
        float spatialCellSize = 0.0f; // uniform grid 의 cell 하나가 월드에서 차지하는 한 변의 길이
        float aoiEnterRadius = 0.0f;  // 새 entity 를 visible set에 추가하는 radius
        float aoiRetainRadius = 0.0f; // 기존 visible entity 를 유지하는 radius

        [[nodiscard]] friend bool operator==(const WorldSpatialConfig& left,
                                             const WorldSpatialConfig& right) noexcept = default;
    };

    // World Entity 하나를 spatial index 에 등록하기 위한 프록시 객체
    // physics 계산 이후 확정된(commit) 위치 사용
    struct WorldSpatialProxy final
    {
        WorldEntityKey entityKey{};
        WorldEntityKind entityKind = WorldEntityKind::Invalid;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float circleRadius = 0.0f; // AOI visibility 용, Physics의 circle radius와 다른 값

        [[nodiscard]] friend bool operator==(const WorldSpatialProxy& left,
                                             const WorldSpatialProxy& right) noexcept = default;
    };

    struct WorldSpatialProxyEntityKeyLess final
    {
        [[nodiscard]] bool operator()(const WorldSpatialProxy& left, const WorldSpatialProxy& right) const noexcept
        {
            return left.entityKey < right.entityKey;
        }
    };

    [[nodiscard]] bool IsValid(const WorldSpatialConfig& config) noexcept;
    [[nodiscard]] bool IsValid(const WorldSpatialProxy& proxy) noexcept;
} // namespace psnr::world
