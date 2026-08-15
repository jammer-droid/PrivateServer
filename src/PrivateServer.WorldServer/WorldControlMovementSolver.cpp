#include "pch.h"

#include "WorldControlMovementSolver.h"

#include <cmath>
#include <numbers>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] bool IsValid(const WorldControlMovementInput& input) noexcept
        {
            return std::isfinite(input.headingRadians) &&
                   (input.turnState == WorldTurnState::Straight || input.turnState == WorldTurnState::Left ||
                    input.turnState == WorldTurnState::Right);
        }

        [[nodiscard]] float TurnSign(const WorldTurnState turnState) noexcept
        {
            if (turnState == WorldTurnState::Left)
            {
                return 1.0f;
            }
            if (turnState == WorldTurnState::Right)
            {
                return -1.0f;
            }
            return 0.0f;
        }

        // 계속 증가하거나 감소하는 각도를
        // 동일한 방향을 나타내는 [-pi, pi) 범위로 정규화
        [[nodiscard]] float NormalizeHeading(const float headingRadians) noexcept
        {
            constexpr float Pi = std::numbers::pi_v<float>;
            constexpr float TwoPi = Pi * 2.0f;

            // fmod(..., 2pi) 로 부동 소수점 나머지 계산
            // fmod(x, y) = x - trunc(x / y) * y
            // - x = 몫 * y + 나머지
            // - 나머지 = x - 몫 * y
            //      - trunc 는 소수점 이하를 0 방향으로 버림
            //      - 2pi + 0.5 -> 0.5
            //      - 4pi + 0.5 -> 0.5
            //      - 음수 반환 가능
            float normalized = std::fmod(headingRadians + Pi, TwoPi);
            if (normalized < 0.0f)
            {
                // 음수인 경우 2pi 더해 [0, 2pi) 범위 지정
                normalized += TwoPi;
            }

            // -pi 로 [-pi, pi) 로 정규화
            return normalized - Pi;
        }
    } // namespace

    bool WorldControlMovementSolver::IsValidConfig(const WorldControlMovementConfig& config) noexcept
    {
        return std::isfinite(config.baseSpeed) && config.baseSpeed > 0.0f && std::isfinite(config.boostSpeed) &&
               config.boostSpeed >= config.baseSpeed && std::isfinite(config.angularSpeedRadiansPerSecond) &&
               config.angularSpeedRadiansPerSecond > 0.0f;
    }

    WorldResult<WorldControlMovementOutput> WorldControlMovementSolver::Solve(const WorldControlMovementConfig& config,
                                                                              const WorldControlMovementInput& input,
                                                                              const float fixedDeltaSeconds) noexcept
    {
        if (!IsValidConfig(config))
        {
            return WorldResult<WorldControlMovementOutput>::Failure(WorldErrorCode::InvalidConfig);
        }
        if (!IsValid(input) || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f)
        {
            return WorldResult<WorldControlMovementOutput>::Failure(WorldErrorCode::InvalidInput);
        }

        const float headingRadians = NormalizeHeading(
            input.headingRadians + TurnSign(input.turnState) * config.angularSpeedRadiansPerSecond * fixedDeltaSeconds);
        const float directionX = std::cos(headingRadians);
        const float directionY = std::sin(headingRadians);
        const float speed = input.boostActive ? config.boostSpeed : config.baseSpeed;

        return WorldResult<WorldControlMovementOutput>(WorldControlMovementOutput{
            headingRadians,
            directionX,
            directionY,
            speed,
            directionX * speed,
            directionY * speed,
        });
    }
} // namespace psnr::world
