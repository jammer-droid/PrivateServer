#pragma once

#include "WorldEntityComponents.h"
#include "WorldResult.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldControlMovementConfig final
    {
        float baseSpeed = 0.0f;
        float boostSpeed = 0.0f;
        float angularSpeedRadiansPerSecond = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldControlMovementConfig& left,
                                             const WorldControlMovementConfig& right) noexcept = default;
    };

    struct WorldControlMovementInput final
    {
        float headingRadians = 0.0f;
        WorldTurnState turnState = WorldTurnState::Straight;
        bool boostActive = false;

        [[nodiscard]] friend bool operator==(const WorldControlMovementInput& left,
                                             const WorldControlMovementInput& right) noexcept = default;
    };

    struct WorldControlMovementOutput final
    {
        float headingRadians = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float speed = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldControlMovementOutput& left,
                                             const WorldControlMovementOutput& right) noexcept = default;
    };

    class WorldControlMovementSolver final
    {
    public:
        [[nodiscard]] static bool IsValidConfig(const WorldControlMovementConfig& config) noexcept;
        [[nodiscard]] static WorldResult<WorldControlMovementOutput> Solve(const WorldControlMovementConfig& config,
                                                                           const WorldControlMovementInput& input,
                                                                           float fixedDeltaSeconds) noexcept;
    };
} // namespace psnr::world
