#pragma once

#include "WorldEntityManager.h"
#include "WorldGameplayConfig.h"
#include "WorldGameplayPhaseResult.h"
#include "WorldGameplayState.h"
#include "WorldPlayerSpawnPlanner.h"
#include "WorldSessionRegistry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    enum class WorldGameplayEntityRemoveReason : std::uint8_t
    {
        Invalid = 0,
        Collected,
        PlayerDeath,
        OutsideActiveArea,
        RoundReset,
    };

    struct WorldGameplayEntityRemoval final
    {
        WorldEntityKey entityKey{};
        WorldGameplayEntityRemoveReason reason = WorldGameplayEntityRemoveReason::Invalid;

        [[nodiscard]] friend bool operator==(const WorldGameplayEntityRemoval& left,
                                             const WorldGameplayEntityRemoval& right) noexcept = default;
    };

    struct WorldGameplayScoreSnapshot final
    {
        std::uint32_t playerId = 0;
        WorldEntityKey controlledEntityKey{};
        std::uint32_t score = 0;

        [[nodiscard]] friend bool operator==(const WorldGameplayScoreSnapshot& left,
                                             const WorldGameplayScoreSnapshot& right) noexcept = default;
    };

    struct WorldGameplayPlayerSpawn final
    {
        std::uint32_t playerId = 0;
        WorldSessionKey sessionKey{};
        WorldEntityKey previousEntityKey{};
        WorldEntityKey spawnedEntityKey{};

        [[nodiscard]] friend bool operator==(const WorldGameplayPlayerSpawn& left,
                                             const WorldGameplayPlayerSpawn& right) noexcept = default;
    };

    struct WorldGameplayCommitReport final
    {
        std::uint32_t serverTick = 0;
        std::vector<WorldGameplayEntityRemoval> entityRemovals;
        std::vector<WorldEntityKey> spawnedResourceKeys;
        std::vector<WorldGameplayPlayerSpawn> playerSpawns;
        std::vector<WorldGameplayScoreSnapshot> scoreSnapshots;
        bool roundSnapshotChanged = false;
        WorldRoundRuntimeState roundSnapshot{};
        bool forceAoiReplication = false;
        std::uint32_t pickupCount = 0;
        std::uint32_t resourceSpawnCount = 0;
    };

    enum class WorldGameplayCommitResult : std::uint8_t
    {
        Committed = 0,
        InvalidArgument,
        InvalidConfig,
        StateInvariantViolation,
        ArithmeticOverflow,
        AllocationFailed,
        EntityRemoveFailed,
        EntityCreateFailed,
        RollbackFailed,
    };

    class WorldGameplayCommitter final
    {
    public:
        [[nodiscard]] static WorldGameplayCommitResult CommitPickups(const WorldGameplayConfig& config,
                                                                     const WorldGameplayPhaseResult& phaseResult,
                                                                     WorldEntityManager& entityManager,
                                                                     WorldGameplayState& gameplayState,
                                                                     WorldGameplayCommitReport* outReport) noexcept;

        [[nodiscard]] static WorldGameplayCommitResult Commit(const WorldGameplayConfig& config,
                                                              const WorldGameplayPhaseResult& phaseResult,
                                                              WorldEntityManager& entityManager,
                                                              WorldGameplayState& gameplayState,
                                                              WorldGameplayCommitReport* outReport) noexcept;

        [[nodiscard]] static WorldGameplayCommitResult CommitPlayerSpawns(
            std::span<const WorldPlayerSpawnCandidate> candidates, WorldEntityManager& entityManager,
            WorldGameplayState& gameplayState, WorldSessionRegistry& sessionRegistry,
            WorldGameplayCommitReport* inOutReport) noexcept;
    };
} // namespace psnr::world
