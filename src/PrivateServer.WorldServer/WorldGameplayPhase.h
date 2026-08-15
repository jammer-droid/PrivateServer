#pragma once

#include "WorldActiveAreaSolver.h"
#include "WorldGameplayConfig.h"
#include "WorldGameplayPhaseResult.h"
#include "WorldGameplayState.h"
#include "WorldPhysicsStepResult.h"
#include "WorldReadView.h"
#include "WorldResult.h"

#include <cstdint>
#include <span>

namespace psnr::world
{
    struct WorldDisconnectDropSnapshot final
    {
        std::uint32_t playerId = 0;
        WorldEntityKey sourceEntityKey{};
        std::uint32_t growthPoint = 0;
        TransformComponent headTransform{};
        BodyTrailComponent bodyTrail{};
    };

    class WorldGameplayPhase final
    {
    public:
        [[nodiscard]] static WorldResult<WorldGameplayPhaseResult> Compute(
            std::uint32_t serverTick, const WorldGameplayConfig& config, const WorldPhysicsStepResult& physicsResult,
            const WorldReadView& postMovementReadView, const WorldRoundRuntimeState& roundState,
            std::span<const WorldPlayerScore> playerScores, std::span<const WorldResourceSlotState> resourceSlots,
            const WorldResourceRegistry& resourceRegistry, std::span<const WorldEntityKey> collisionDeathSet,
            std::span<const WorldEntityKey> activeAreaBoundaryDeathSet, float fixedDeltaSeconds,
            const WorldActiveArea* activeArea) noexcept;
        [[nodiscard]] static WorldResult<WorldGameplayPhaseResult> Compute(
            std::uint32_t serverTick, const WorldGameplayConfig& config, const WorldPhysicsStepResult& physicsResult,
            const WorldReadView& postMovementReadView, const WorldRoundRuntimeState& roundState,
            std::span<const WorldPlayerScore> playerScores, std::span<const WorldResourceSlotState> resourceSlots,
            const WorldResourceRegistry& resourceRegistry, std::span<const WorldEntityKey> collisionDeathSet,
            std::span<const WorldEntityKey> activeAreaBoundaryDeathSet,
            std::span<const WorldDisconnectDropSnapshot> disconnectDropSnapshots, float fixedDeltaSeconds,
            const WorldActiveArea* activeArea) noexcept;
    };
} // namespace psnr::world
