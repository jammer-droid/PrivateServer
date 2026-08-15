#include "pch.h"

#include "WorldBoostCostSolver.h"

#include <cmath>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] bool IsValid(const WorldBoostCostState& state) noexcept
        {
            return std::isfinite(state.boostCostAccumulator) && state.boostCostAccumulator >= 0.0f;
        }
    } // namespace

    bool WorldBoostCostSolver::IsDisabled(const WorldBoostCostConfig& config) noexcept
    {
        return config.growth == WorldGrowthConfig{} && config.initialLengthCostPerSecond == 0.0f;
    }

    bool WorldBoostCostSolver::IsValidConfig(const WorldBoostCostConfig& config) noexcept
    {
        return WorldGrowthSolver::IsValidConfig(config.growth) && std::isfinite(config.initialLengthCostPerSecond) &&
               config.initialLengthCostPerSecond > 0.0f;
    }

    WorldResult<WorldBoostCostState> WorldBoostCostSolver::Solve(const WorldBoostCostConfig& config,
                                                                 const WorldBoostCostState& currentState,
                                                                 const bool boostRequested,
                                                                 const float fixedDeltaSeconds) noexcept
    {
        if (!IsValidConfig(config))
        {
            return WorldResult<WorldBoostCostState>::Failure(WorldErrorCode::InvalidConfig);
        }
        if (!IsValid(currentState) || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f)
        {
            return WorldResult<WorldBoostCostState>::Failure(WorldErrorCode::InvalidInput);
        }

        WorldBoostCostState nextState = currentState;
        // growthPoint == 0 -> 부스트 비활성화
        if (!boostRequested || currentState.growthPoint == 0)
        {
            return WorldResult<WorldBoostCostState>(nextState);
        }

        // 부스트 비용 = 시작 비용 * (전체 길이 / 시작 길이)
        // 누적 비용 = 이전 누적 + 부스트 비용 * dt

        WorldResult<WorldGrowthDimensions> growthResult =
            WorldGrowthSolver::Solve(config.growth, currentState.growthPoint);
        if (growthResult.Failed())
        {
            return WorldResult<WorldBoostCostState>::Failure(WorldErrorCode::InvalidInput);
        }
        const WorldGrowthDimensions growthDimensions = growthResult.TakeValue();
        const float costPerSecond =
            config.initialLengthCostPerSecond * growthDimensions.nominalLength / config.growth.initialLength;
        const float accumulatedCost = currentState.boostCostAccumulator + costPerSecond * fixedDeltaSeconds;
        if (!std::isfinite(costPerSecond) || !std::isfinite(accumulatedCost))
        {
            return WorldResult<WorldBoostCostState>::Failure(WorldErrorCode::InvalidInput);
        }

        // 차감은 정수 부분만
        nextState.boostCostAccumulator = accumulatedCost;
        const float wholePointCost = std::floor(nextState.boostCostAccumulator);
        std::uint32_t spentPointCount = 0;
        if (wholePointCost >= static_cast<float>(nextState.growthPoint))
        {
            spentPointCount = nextState.growthPoint;
        }
        else
        {
            spentPointCount = static_cast<std::uint32_t>(wholePointCost);
        }
        nextState.growthPoint -= spentPointCount;
        nextState.boostCostAccumulator -= static_cast<float>(spentPointCount);

        return WorldResult<WorldBoostCostState>(nextState);
    }
} // namespace psnr::world
