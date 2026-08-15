#pragma once

#include "WorldEntityIdentity.h"
#include "WorldGameplayState.h"
#include "WorldPhysicsValues.h"
#include "WorldResourceSpawnPlanner.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    struct WorldResourcePickup final
    {
        std::uint32_t resourceSlotId = 0;
        WorldEntityKey resourceEntityKey{};
        EntityHandle resourceEntityHandle{};
        PhysicsFixtureId resourceFixtureId{};

        std::uint32_t playerId = 0;
        WorldEntityKey playerEntityKey{};
        EntityHandle playerEntityHandle{};
        PhysicsFixtureId playerFixtureId{};
        std::uint32_t scoreAward = 0;

        [[nodiscard]] static bool ContainsAmbientPickupForSlot(
            const std::span<const WorldResourcePickup> pickups, const std::uint32_t slotId) noexcept
        {
            if (slotId == 0)
            {
                return false;
            }
            for (const WorldResourcePickup& pickup : pickups)
            {
                if (pickup.resourceSlotId == slotId)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] friend bool operator==(const WorldResourcePickup& left,
                                             const WorldResourcePickup& right) noexcept = default;
    };

    struct WorldPlayerScoreAward final
    {
        std::uint32_t playerId = 0;
        WorldEntityKey controlledEntityKey{};
        std::uint32_t scoreAward = 0;

        [[nodiscard]] friend bool operator==(const WorldPlayerScoreAward& left,
                                             const WorldPlayerScoreAward& right) noexcept = default;
    };

    struct WorldPlayerBoostCostCommit final
    {
        std::uint32_t playerId = 0;
        WorldEntityKey controlledEntityKey{};
        WorldBoostCostState nextState{};

        [[nodiscard]] friend bool operator==(const WorldPlayerBoostCostCommit& left,
                                             const WorldPlayerBoostCostCommit& right) noexcept = default;
    };

    enum class WorldPlayerDeathCauseFlags : std::uint8_t
    {
        None = 0,
        Collision = 1u << 0,
        ActiveAreaBoundary = 1u << 1,
    };

    [[nodiscard]] constexpr WorldPlayerDeathCauseFlags operator|(const WorldPlayerDeathCauseFlags left,
                                                                 const WorldPlayerDeathCauseFlags right) noexcept
    {
        return static_cast<WorldPlayerDeathCauseFlags>(static_cast<std::uint8_t>(left) |
                                                       static_cast<std::uint8_t>(right));
    }

    struct WorldPlayerDeathCommit final
    {
        std::uint32_t playerId = 0;
        WorldEntityKey controlledEntityKey{};
        WorldPlayerDeathCauseFlags causes = WorldPlayerDeathCauseFlags::None;

        [[nodiscard]] friend bool operator==(const WorldPlayerDeathCommit& left,
                                             const WorldPlayerDeathCommit& right) noexcept = default;
    };

    struct WorldDisconnectDropCommit final
    {
        std::uint32_t playerId = 0;
        WorldEntityKey sourceEntityKey{};
        std::uint32_t growthPoint = 0;

        [[nodiscard]] friend bool operator==(const WorldDisconnectDropCommit& left,
                                             const WorldDisconnectDropCommit& right) noexcept = default;
    };

    // Round 전환 상태
    enum class WorldGameplayRoundTransitionKind : std::uint8_t
    {
        None = 0,
        Started,
        Ended,
        ResetToRunning,
        ResetToWaiting,
    };

    // Round 상태 전환 종류, 전환 후의 상태를 보관
    struct WorldGameplayRoundTransition final
    {
        WorldGameplayRoundTransitionKind kind = WorldGameplayRoundTransitionKind::None; // 상태 전환 이유
        WorldRoundRuntimeState nextState{};                                             // 전환 후 라운드 상태

        [[nodiscard]] friend bool operator==(const WorldGameplayRoundTransition& left,
                                             const WorldGameplayRoundTransition& right) noexcept = default;
    };

    class WorldGameplayPhaseResult final
    {
    public:
        WorldGameplayPhaseResult() = default;
        WorldGameplayPhaseResult(std::uint32_t serverTick, std::vector<WorldResourcePickup> pickups,
                                 std::vector<WorldPlayerScoreAward> scoreAwards) noexcept;
        WorldGameplayPhaseResult(std::uint32_t serverTick, std::vector<WorldResourcePickup> pickups,
                                 std::vector<WorldPlayerScoreAward> scoreAwards,
                                 std::vector<WorldResourceSpawnRequest> resourceSpawns,
                                 WorldGameplayRoundTransition roundTransition,
                                 std::vector<WorldPlayerBoostCostCommit> boostCosts = {},
                                 std::vector<WorldPlayerDeathCommit> playerDeaths = {},
                                 std::vector<WorldResourceSpawnRequest> deathDropSpawns = {},
                                 std::uint32_t deathDropPlacementFailureCount = 0,
                                 std::vector<WorldResourceInstance> outsideResourceRemovals = {},
                                 std::vector<WorldDisconnectDropCommit> disconnectDrops = {}) noexcept;

        [[nodiscard]] std::uint32_t ServerTick() const noexcept;
        [[nodiscard]] std::span<const WorldResourcePickup> Pickups() const noexcept;
        [[nodiscard]] std::span<const WorldPlayerScoreAward> ScoreAwards() const noexcept;
        [[nodiscard]] std::span<const WorldPlayerBoostCostCommit> BoostCosts() const noexcept;
        [[nodiscard]] std::span<const WorldPlayerDeathCommit> PlayerDeaths() const noexcept;
        [[nodiscard]] std::span<const WorldDisconnectDropCommit> DisconnectDrops() const noexcept;
        [[nodiscard]] std::span<const WorldResourceSpawnRequest> DeathDropSpawns() const noexcept;
        [[nodiscard]] std::uint32_t DeathDropPlacementFailureCount() const noexcept;
        [[nodiscard]] std::span<const WorldResourceInstance> OutsideResourceRemovals() const noexcept;
        [[nodiscard]] std::span<const WorldResourceSpawnRequest> ResourceSpawns() const noexcept;
        [[nodiscard]] const WorldGameplayRoundTransition& RoundTransition() const noexcept;

    private:
        std::uint32_t serverTick_ = 0;
        std::vector<WorldResourcePickup> pickups_;
        std::vector<WorldPlayerScoreAward> scoreAwards_;
        std::vector<WorldPlayerBoostCostCommit> boostCosts_;
        std::vector<WorldPlayerDeathCommit> playerDeaths_;
        std::vector<WorldDisconnectDropCommit> disconnectDrops_;
        std::vector<WorldResourceSpawnRequest> deathDropSpawns_;
        std::uint32_t deathDropPlacementFailureCount_ = 0;
        std::vector<WorldResourceInstance> outsideResourceRemovals_;
        std::vector<WorldResourceSpawnRequest> resourceSpawns_;
        WorldGameplayRoundTransition roundTransition_{};
    };
} // namespace psnr::world
