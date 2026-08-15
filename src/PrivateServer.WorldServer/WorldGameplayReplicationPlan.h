#pragma once

#include "ControlledEntityState.h"
#include "EntityStateBatch.h"
#include "EntityRemove.h"
#include "RoundResult.h"
#include "RoundState.h"
#include "ScoreState.h"
#include "WorldAoiPlanner.h"
#include "WorldEntityManager.h"
#include "WorldGameplayCommitter.h"
#include "WorldOverviewPlanner.h"
#include "WorldRoundResultPlanner.h"
#include "WorldSessionRegistry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    enum class WorldGameplayJoinPacketKind : std::uint8_t
    {
        ControlledEntitySpawn = 0,
        ScoreState,
        RoundState,
        WorldReady,
    };

    struct WorldGameplayEntityRemovalPlan final
    {
        WorldSessionKey sessionKey{};
        protocol::v1::EntityRemove entityRemove{};

        [[nodiscard]] friend bool operator==(const WorldGameplayEntityRemovalPlan& left,
                                             const WorldGameplayEntityRemovalPlan& right) noexcept = default;
    };

    struct WorldGameplayRoundResultPlan final
    {
        WorldSessionKey sessionKey{};
        protocol::v2::RoundResult roundResult{};

        [[nodiscard]] friend bool operator==(const WorldGameplayRoundResultPlan& left,
                                             const WorldGameplayRoundResultPlan& right) noexcept = default;
    };

    // Gameplay 갱신 결과를 전달할 DTO
    struct WorldGameplayReplicationPlan final
    {
        std::uint32_t serverTick = 0;
        std::vector<WorldSessionKey> worldBroadcastRecipients;
        std::vector<protocol::v1::ScoreState> scoreStates;
        std::vector<WorldGameplayEntityRemovalPlan> entityRemovals;
        std::vector<WorldGameplayRoundResultPlan> roundResults;
        bool hasRoundState = false;
        protocol::v1::RoundState roundState{};
        std::vector<WorldGameplayJoinPacketKind> joinPacketOrder;
    };

    // Gameplay 결과를 publish 하기 위한 DTO 생성 Planner
    class WorldGameplayReplicationPlanner final
    {
    public:
        [[nodiscard]] WorldResult<WorldGameplayReplicationPlan> BuildScoreBroadcast(
            const WorldGameplayCommitReport& commitReport, std::span<const WorldSession> joinedSessions) const noexcept;

        [[nodiscard]] WorldResult<WorldGameplayReplicationPlan> BuildBroadcast(
            const WorldGameplayConfig& config, const WorldGameplayCommitReport& commitReport,
            std::span<const WorldSession> joinedSessions) const noexcept;

        [[nodiscard]] WorldResult<WorldGameplayReplicationPlan> BuildJoinBaseline(
            std::uint32_t serverTick, const WorldGameplayConfig& config,
            const WorldGameplayState& gameplayState) const noexcept;

        [[nodiscard]] WorldResult<WorldGameplayReplicationPlan> BuildEntityRemovals(
            const WorldGameplayCommitReport& commitReport, std::span<const WorldAoiPrunedVisibility> prunedVisibilities,
            std::span<const WorldSession> joinedSessions) const noexcept;

        [[nodiscard]] WorldResult<protocol::v2::ControlledEntityState> BuildControlledEntityState(
            std::uint32_t serverTick, const WorldSession& session, const WorldPlayerScore& score,
            const WorldEntityComponents& components) const noexcept;

        [[nodiscard]] WorldResult<std::vector<protocol::v2::EntityStateBatch>> BuildRemoteEntityStateChunks(
            std::uint32_t serverTick, std::uint32_t snapshotId, std::span<const WorldEntityKey> visibleEntityKeys,
            const WorldGameplayState& gameplayState, const WorldEntityManager& entityManager) const noexcept;

        [[nodiscard]] WorldResult<WorldOverviewPlanInput> BuildOverviewInput(
            std::uint32_t serverTick, std::uint32_t overviewId, const WorldPhysicsArenaBounds& mapBounds,
            const WorldActiveArea& activeArea, const WorldGameplayState& gameplayState,
            const WorldEntityManager& entityManager) const noexcept;
        [[nodiscard]] WorldResult<WorldGameplayReplicationPlan> BuildRoundResults(
            std::uint32_t endTick, std::uint32_t roundId, std::span<const WorldPlayerScore> playerScores,
            std::span<const WorldSession> joinedSessions) const noexcept;

    private:
        [[nodiscard]] static bool RecipientLess(WorldSessionKey left, WorldSessionKey right) noexcept;
        [[nodiscard]] static bool EntityRemovalPlanLess(const WorldGameplayEntityRemovalPlan& left,
                                                        const WorldGameplayEntityRemovalPlan& right) noexcept;
        [[nodiscard]] static protocol::RoundPhase ToProtocolRoundPhase(WorldRoundPhase phase) noexcept;
        [[nodiscard]] static protocol::v1::RoundState MakeRoundState(std::uint32_t serverTick,
                                                                     const WorldGameplayConfig& config,
                                                                     const WorldRoundRuntimeState& roundState) noexcept;
        [[nodiscard]] static const WorldGameplayEntityRemoval* FindEntityRemoval(
            const std::vector<WorldGameplayEntityRemoval>& removals, WorldEntityKey entityKey) noexcept;
        [[nodiscard]] static protocol::EntityRemoveReason ToProtocolRemoveReason(
            WorldGameplayEntityRemoveReason reason) noexcept;
        [[nodiscard]] static protocol::v2::BoostState ToProtocolBoostState(WorldBoostState state) noexcept;
    };
} // namespace psnr::world
