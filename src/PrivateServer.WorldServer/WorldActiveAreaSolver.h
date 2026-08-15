#pragma once

#include "WorldPhysicsValues.h"
#include "WorldResult.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldActiveAreaConfig final
    {
        WorldPhysicsArenaBounds mapBounds{};
        std::uint32_t roundDurationTicks = 0;
        float startRatio = 0.0f;
        float endRatio = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldActiveAreaConfig& left,
                                             const WorldActiveAreaConfig& right) noexcept = default;
    };

    struct WorldActiveArea final // 특정 server tick 에서 실제 적용되는 원의 계산 결과
    {
        float centerX = 0.0f;
        float centerY = 0.0f;
        float referenceRadius = 0.0f; // Map Bounds 안에 완전히 들어가는 가장 큰 원의 반지름(수축 비율 1.0)
        float ratio = 0.0f;           // 수축 비율 (기본값 기준 1.0 -> 0.25 로 선형 감소)
        float radius = 0.0f;          // 현재 tick에 적용되는 반지름

        // invalid area/circle과 경계 접촉을 포함한 outside circle에는 false를 반환한다.
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool ContainsCircleStrictly(float circleCenterX, float circleCenterY,
                                                  float circleRadius) const noexcept;

        [[nodiscard]] friend bool operator==(const WorldActiveArea& left,
                                             const WorldActiveArea& right) noexcept = default;
    };

    class WorldActiveAreaSolver final
    {
    public:
        [[nodiscard]] static bool IsValidConfig(const WorldActiveAreaConfig& config) noexcept;

        [[nodiscard]] static WorldResult<WorldActiveArea> Solve(const WorldActiveAreaConfig& config,
                                                                std::uint32_t roundStartTick,
                                                                std::uint32_t serverTick) noexcept;
    };
} // namespace psnr::world
