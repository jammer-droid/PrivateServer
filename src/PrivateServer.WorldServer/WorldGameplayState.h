#pragma once

#include "WorldEntityIdentity.h"
#include "WorldGameplayConfig.h"
#include "WorldResult.h"
#include "WorldResourceRegistry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    class WorldGameplayCommitter;

    enum class WorldRoundPhase : std::uint8_t // 현재 라운드의 진행 단계
    {
        Invalid = 0,
        Waiting, // 플레이어 대기
        Running, // 게임 진행 중
        Ended,   // 종료
    };

    enum class WorldResourceSlotPhase : std::uint8_t // 각 리소스 슬롯의 생명 주기
    {
        Invalid = 0,
        Dormant,    // 슬롯은 존재하지만, Resource Entity 는 아직 없음
        Active,     // Resource Entity 가 월드에 생성되어 획득 가능
        Respawning, // active entity가 없고 density refill에 재사용 가능
    };

    enum class WorldPlayerLifecycle : std::uint8_t
    {
        Invalid = 0,
        SpawnPending,
        Alive,
    };

    struct WorldRoundRuntimeState final // 현재 라운드 전체의 상태
    {
        std::uint32_t roundId = 0; // 라운드 식별자
        WorldRoundPhase phase = WorldRoundPhase::Invalid;
        std::uint32_t phaseEndsAtServerTick = 0; // 현재 단계 종료 시점의 서버 tick
        std::uint32_t winnerPlayerId = 0;        // 승리한 player Id

        [[nodiscard]] friend bool operator==(const WorldRoundRuntimeState& left,
                                             const WorldRoundRuntimeState& right) noexcept = default;
    };

    struct WorldPlayerScore final // 플레이어 한 명의 게임 점수 상태
    {
        std::uint32_t playerId = 0;           // 플레이어 ID
        WorldEntityKey controlledEntityKey{}; // 플레이어가 조작하는 월드 Entity
        std::uint32_t score = 0;              // 점수
        float boostCostAccumulator = 0.0f;
        WorldPlayerLifecycle lifecycle = WorldPlayerLifecycle::Alive;

        [[nodiscard]] friend bool operator==(const WorldPlayerScore& left,
                                             const WorldPlayerScore& right) noexcept = default;
    };

    struct WorldResourceSlotState final // 리소스 슬롯 상태
    {
        std::uint32_t slotId = 0;
        WorldResourceSlotPhase phase = WorldResourceSlotPhase::Invalid;

        [[nodiscard]] friend bool operator==(const WorldResourceSlotState& left,
                                             const WorldResourceSlotState& right) noexcept = default;
    };

    struct WorldGameplayMetrics final
    {
        std::uint64_t deathDropPlacementFailureCount = 0;

        [[nodiscard]] friend bool operator==(const WorldGameplayMetrics& left,
                                             const WorldGameplayMetrics& right) noexcept = default;
    };

    struct WorldPlayerScoreIdLess final
    {
        [[nodiscard]] bool operator()(const WorldPlayerScore& score, std::uint32_t playerId) const noexcept;
    };

    class WorldPlayerScoreLookup final
    {
    public:
        [[nodiscard]] static const WorldPlayerScore* FindByPlayerId(std::span<const WorldPlayerScore> playerScores,
                                                                    std::uint32_t playerId) noexcept;
        [[nodiscard]] static WorldPlayerScore* FindMutableByPlayerId(std::span<WorldPlayerScore> playerScores,
                                                                     std::uint32_t playerId) noexcept;
        [[nodiscard]] static const WorldPlayerScore* FindByEntityKey(std::span<const WorldPlayerScore> playerScores,
                                                                     WorldEntityKey entityKey) noexcept;
    };

    enum class WorldPlayerScoreRegisterResult : std::uint8_t
    {
        Registered = 0,
        InvalidArgument,
        DuplicatePlayer,
        IdentityConflict,
        AllocationFailed,
    };

    class WorldGameplayState final
    {
    public:
        [[nodiscard]] WorldPlayerScoreRegisterResult TryRegisterPlayer(std::uint32_t playerId,
                                                                       WorldEntityKey controlledEntityKey) noexcept;
        [[nodiscard]] bool RemovePlayer(std::uint32_t playerId, WorldEntityKey controlledEntityKey) noexcept;
        [[nodiscard]] bool TryFindPlayerScore(std::uint32_t playerId, WorldPlayerScore* outScore) const noexcept;

        [[nodiscard]] const WorldRoundRuntimeState& RoundState() const noexcept;
        [[nodiscard]] std::span<const WorldPlayerScore> PlayerScores() const noexcept;
        [[nodiscard]] std::span<const WorldResourceSlotState> ResourceSlots() const noexcept;
        [[nodiscard]] const WorldResourceRegistry& ResourceRegistry() const noexcept;
        [[nodiscard]] WorldGameplayMetrics Metrics() const noexcept;
        [[nodiscard]] std::size_t PlayerCount() const noexcept;

    private:
        friend class WorldGameplayCommitter;
        friend WorldResult<WorldGameplayState> CreateWorldGameplayState(
            const WorldGameplayConfig& config, const WorldPhysicsArenaBounds& arenaBounds) noexcept;

        WorldRoundRuntimeState roundState_{};
        std::vector<WorldPlayerScore> playerScores_;
        std::vector<WorldResourceSlotState> resourceSlots_;
        WorldResourceRegistry resourceRegistry_;
        WorldGameplayMetrics metrics_{};
    };

    [[nodiscard]] WorldResult<WorldGameplayState> CreateWorldGameplayState(
        const WorldGameplayConfig& config, const WorldPhysicsArenaBounds& arenaBounds) noexcept;
} // namespace psnr::world
