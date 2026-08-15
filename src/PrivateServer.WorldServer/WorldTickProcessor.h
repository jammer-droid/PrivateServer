#pragma once

#include "WorldActiveAreaSolver.h"
#include "WorldBodyTrailSampler.h"
#include "WorldCollisionDeathResolver.h"
#include "WorldCollisionProxyBatch.h"
#include "WorldControlMovementSolver.h"
#include "WorldEntityManager.h"
#include "WorldGameplayState.h"
#include "WorldMovementCommandStore.h"
#include "WorldMovementTickInputBuilder.h"
#include "WorldPhysicsScene.h"
#include "WorldPlayerBody.h"
#include "WorldPlayerSpawnPlanner.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace psnr::world
{
    struct WorldTickProcessorConfig final
    {
        WorldPhysicsConfig physics{};
        WorldPhysicsArenaBounds arenaBounds{};
        WorldControlMovementConfig controlMovement{};
        WorldBodyTrailSampleConfig bodyTrailSample{};
        WorldPlayerBodyConfig playerBody{};
        std::uint32_t playerArchetypeId = 0;
        std::uint32_t playerSpawnMaxCandidatesPerTick = 0;
    };

    enum class WorldTickProcessResult : std::uint8_t
    {
        Processed = 0,
        InvalidFixedDelta,
        InvalidControlMovementConfig,
        InvalidSessionSet,
        NonSequentialServerTick,
        EntityStateInvariantViolation,
        PhysicsInitialPenetration,
        PhysicsComputeFailed,
        BodyTrailSampleFailed,
        BodyFinalizeFailed,
        CollisionProjectionFailed,
        CollisionQueryFailed,
        CollisionDeathResolveFailed,
        InvalidActiveArea,
        ActiveAreaDeathCollectFailed,
        PlayerSpawnPlanFailed,
        PlayerSpawnReservationFailed,
        GameplayProcessFailed,
    };

    // 하나의 fixed tick에서 입력 확정, read-only 계산, deterministic commit을 순서대로 수행한다.
    // World owner thread만 이 객체와 전달된 mutable store/manager를 사용한다.
    class WorldTickProcessor final
    {
    public:
        WorldTickProcessor() = default;
        explicit WorldTickProcessor(const WorldTickProcessorConfig& config) noexcept;

        [[nodiscard]] WorldTickProcessResult Process(WorldInboundMode inboundMode, std::uint32_t serverTick,
                                                     float fixedDeltaSeconds,
                                                     std::span<const WorldSession> joinedSessions,
                                                     WorldMovementCommandStore& commandStore,
                                                     WorldEntityManager& entityManager,
                                                     std::span<const WorldPlayerScore> playerScores = {},
                                                     const WorldActiveArea* activeArea = nullptr);

        [[nodiscard]] WorldTickProcessResult Process(std::uint32_t serverTick, float fixedDeltaSeconds,
                                                     std::span<const WorldSession> joinedSessions,
                                                     WorldMovementCommandStore& commandStore,
                                                     WorldEntityManager& entityManager,
                                                     std::span<const WorldPlayerScore> playerScores = {},
                                                     const WorldActiveArea* activeArea = nullptr);

        [[nodiscard]] bool PhysicsEnabled() const noexcept;
        [[nodiscard]] const WorldPhysicsStepResult& LastPhysicsResult() const noexcept;
        [[nodiscard]] std::span<const WorldEntityKey> LastCollisionDeathSet() const noexcept;
        [[nodiscard]] std::span<const WorldEntityKey> LastActiveAreaBoundaryDeathSet() const noexcept;
        [[nodiscard]] std::span<const WorldPlayerSpawnCandidate> LastPlayerSpawnCandidates() const noexcept;

    private:
        [[nodiscard]] WorldTickProcessResult ProcessWithPhysics(std::uint32_t serverTick,
                                                                std::span<const WorldMovementTickInput> movementInputs,
                                                                float fixedDeltaSeconds,
                                                                WorldEntityManager& entityManager,
                                                                std::span<const WorldPlayerScore> playerScores,
                                                                const WorldActiveArea* activeArea);
        [[nodiscard]] WorldTickProcessResult CollectActiveAreaBoundaryDeaths(
            const WorldActiveArea* activeArea, std::span<const WorldPlayerScore> playerScores,
            const WorldEntityManager& entityManager, std::vector<WorldEntityKey>* outDeathSet) const;
        [[nodiscard]] WorldTickProcessResult ProcessPlayerCollisions(std::span<const WorldPlayerScore> playerScores,
                                                                     WorldEntityManager& entityManager);
        [[nodiscard]] WorldTickProcessResult PlanPlayerSpawns(std::uint32_t serverTick,
                                                              std::span<const WorldPlayerScore> playerScores,
                                                              const WorldActiveArea* activeArea);

        WorldTickProcessorConfig config_{};
        std::unique_ptr<WorldPhysicsScene> physicsScene_;
        WorldPhysicsStepResult lastPhysicsResult_;
        WorldCollisionProxyBatch collisionProxyBatch_;
        std::vector<WorldEntityKey> lastCollisionDeathSet_;
        std::vector<WorldEntityKey> lastActiveAreaBoundaryDeathSet_;
        std::vector<WorldPlayerSpawnCandidate> lastPlayerSpawnCandidates_;
        WorldMovementTickInputBuilder movementInputBuilder_;
    };
} // namespace psnr::world
