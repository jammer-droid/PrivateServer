#include "pch.h"

#include "WorldGameplayCommitter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace psnr::world
{
    static_assert(std::is_nothrow_move_assignable_v<WorldResourceRegistry>);

    namespace
    {
        struct CreatedResource final
        {
            WorldResourceOrigin origin = WorldResourceOrigin::Invalid;
            std::uint32_t ambientSlotId = 0;
            WorldEntityKey entityKey{};
            EntityHandle entityHandle{};
            float positionX = 0.0f;
            float positionY = 0.0f;
        };

        struct PreparedPlayerDeath final
        {
            WorldPlayerDeathCommit death{};
            EntityHandle entityHandle{};
        };

        struct PreparedPlayerSpawn final
        {
            const WorldPlayerSpawnCandidate* candidate = nullptr;
            WorldPlayerScore* score = nullptr;
            WorldSessionKey sessionKey{};
            WorldEntityKey previousEntityKey{};
        };

        struct CreatedPlayer final
        {
            std::uint32_t playerId = 0;
            WorldSessionKey sessionKey{};
            WorldEntityKey previousEntityKey{};
            WorldEntityKey spawnedEntityKey{};
            EntityHandle spawnedEntityHandle{};
            WorldPlayerScore* score = nullptr;
        };

        [[nodiscard]] WorldEntityComponents MakeResourceComponents(const WorldGameplayConfig& config,
                                                                   const float positionX, const float positionY)
        {
            WorldEntityComponents components;
            components.transform = TransformComponent{positionX, positionY, 0.0f};
            components.motion = MotionComponent{};
            components.movementCapability = MovementCapabilityComponent{0.0f};
            components.replicationMetadata = ReplicationMetadataComponent{
                WorldEntityKind::Resource,
                config.resourceArchetypeId,
                WorldShapeKind::Circle,
                config.resourceCircleRadius,
            };
            components.playerControl = PlayerControlComponent{0};
            components.physicsBinding = PhysicsBindingComponent{};
            return components;
        }

        // 생성의 역순으로 rollback
        [[nodiscard]] bool RollbackCreatedResources(const std::vector<CreatedResource>& created,
                                                    WorldEntityManager& entityManager) noexcept
        {
            bool succeeded = true;
            for (std::vector<CreatedResource>::const_reverse_iterator iterator = created.rbegin();
                 iterator != created.rend(); ++iterator)
            {
                if (!entityManager.Remove(iterator->entityHandle))
                {
                    succeeded = false;
                }
            }

            return succeeded;
        }

        [[nodiscard]] WorldGameplayCommitResult CreateResource(const WorldGameplayConfig& config,
                                                               const WorldResourceSpawnRequest& request,
                                                               WorldEntityManager& entityManager,
                                                               std::vector<CreatedResource>* const created)
        {
            if (created == nullptr)
            {
                return WorldGameplayCommitResult::InvalidArgument;
            }

            WorldEntityKey entityKey;
            EntityHandle entityHandle;
            const WorldEntityComponents components =
                MakeResourceComponents(config, request.positionX, request.positionY);
            if (!entityManager.TryCreate(components, &entityKey, &entityHandle))
            {
                return WorldGameplayCommitResult::EntityCreateFailed;
            }

            try
            {
                created->push_back(CreatedResource{request.origin, request.ambientSlotId, entityKey, entityHandle,
                                                   request.positionX, request.positionY});
                return WorldGameplayCommitResult::Committed;
            }
            catch (...)
            {
                if (!entityManager.Remove(entityHandle))
                {
                    return WorldGameplayCommitResult::RollbackFailed;
                }
                throw;
            }
        }

        [[nodiscard]] bool RollbackCreatedPlayers(const std::vector<CreatedPlayer>& created,
                                                  const std::size_t reboundSessionCount,
                                                  WorldEntityManager& entityManager,
                                                  WorldSessionRegistry& sessionRegistry) noexcept
        {
            bool succeeded = true;
            for (std::size_t index = reboundSessionCount; index > 0; --index)
            {
                const CreatedPlayer& player = created[index - 1];
                try
                {
                    // prevEntityKey 로 상태 롤백
                    if (!sessionRegistry.TryRebindControlledEntity(player.sessionKey, player.previousEntityKey))
                    {
                        succeeded = false;
                    }
                }
                catch (...)
                {
                    succeeded = false;
                }
            }
            for (std::vector<CreatedPlayer>::const_reverse_iterator iterator = created.rbegin();
                 iterator != created.rend(); ++iterator)
            {
                try
                {
                    if (!entityManager.Remove(iterator->spawnedEntityHandle))
                    {
                        succeeded = false;
                    }
                }
                catch (...)
                {
                    succeeded = false;
                }
            }
            return succeeded;
        }

        [[nodiscard]] bool IsValid(const WorldPlayerSpawnCandidate& candidate) noexcept
        {
            const WorldEntityComponents& components = candidate.components;
            const WorldPlayerSpawnBounds& bounds = candidate.bounds;
            return candidate.playerId != 0 && components.playerControl.playerId == candidate.playerId &&
                   components.replicationMetadata.entityKind == WorldEntityKind::Player &&
                   components.replicationMetadata.primaryShapeKind == WorldShapeKind::Circle &&
                   std::isfinite(components.replicationMetadata.primaryCircleRadius) &&
                   components.replicationMetadata.primaryCircleRadius > 0.0f && components.bodyTrail.IsInitialized() &&
                   !components.bodyTrail.Empty() && WorldPlayerSpawnBounds::IsValid(bounds);
        }

        [[nodiscard]] WorldResourceSlotState* FindResourceSlot(std::vector<WorldResourceSlotState>& resourceSlots,
                                                               const std::uint32_t slotId) noexcept
        {
            const std::vector<WorldResourceSlotState>::iterator found =
                std::lower_bound(resourceSlots.begin(), resourceSlots.end(), slotId,
                                 [](const WorldResourceSlotState& slot, const std::uint32_t expectedSlotId) noexcept
                                 { return slot.slotId < expectedSlotId; });
            if (found == resourceSlots.end() || found->slotId != slotId)
            {
                return nullptr;
            }

            return &*found;
        }

        [[nodiscard]] const WorldPlayerScoreAward* FindScoreAward(
            const std::span<const WorldPlayerScoreAward> scoreAwards, const std::uint32_t playerId) noexcept
        {
            const std::span<const WorldPlayerScoreAward>::iterator found =
                std::lower_bound(scoreAwards.begin(), scoreAwards.end(), playerId,
                                 [](const WorldPlayerScoreAward& award, const std::uint32_t expectedPlayerId) noexcept
                                 { return award.playerId < expectedPlayerId; });
            if (found == scoreAwards.end() || found->playerId != playerId)
            {
                return nullptr;
            }

            return &*found;
        }

        [[nodiscard]] const WorldPlayerDeathCommit* FindPlayerDeath(
            const std::span<const WorldPlayerDeathCommit> playerDeaths,
            const WorldEntityKey controlledEntityKey) noexcept
        {
            for (const WorldPlayerDeathCommit& death : playerDeaths)
            {
                if (death.controlledEntityKey == controlledEntityKey)
                {
                    return &death;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const WorldDisconnectDropCommit* FindDisconnectDrop(
            const std::span<const WorldDisconnectDropCommit> disconnectDrops,
            const WorldEntityKey sourceEntityKey) noexcept
        {
            for (const WorldDisconnectDropCommit& disconnectDrop : disconnectDrops)
            {
                if (disconnectDrop.sourceEntityKey == sourceEntityKey)
                {
                    return &disconnectDrop;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::vector<WorldGameplayScoreSnapshot>::iterator FindScoreSnapshot(
            std::vector<WorldGameplayScoreSnapshot>& snapshots, const std::uint32_t playerId) noexcept
        {
            return std::lower_bound(
                snapshots.begin(), snapshots.end(), playerId,
                [](const WorldGameplayScoreSnapshot& snapshot, const std::uint32_t expectedPlayerId) noexcept
                { return snapshot.playerId < expectedPlayerId; });
        }
    } // namespace

    WorldGameplayCommitResult WorldGameplayCommitter::CommitPickups(const WorldGameplayConfig& config,
                                                                    const WorldGameplayPhaseResult& phaseResult,
                                                                    WorldEntityManager& entityManager,
                                                                    WorldGameplayState& gameplayState,
                                                                    WorldGameplayCommitReport* const outReport) noexcept
    {
        if (outReport == nullptr)
        {
            return WorldGameplayCommitResult::InvalidArgument;
        }
        if (!phaseResult.BoostCosts().empty() && !WorldBoostCostSolver::IsValidConfig(config.boostCost))
        {
            return WorldGameplayCommitResult::InvalidConfig;
        }
        if (phaseResult.Pickups().size() > std::numeric_limits<std::uint32_t>::max())
        {
            return WorldGameplayCommitResult::ArithmeticOverflow;
        }

        try
        {
            std::vector<PreparedPlayerDeath> preparedDeaths;
            preparedDeaths.reserve(phaseResult.PlayerDeaths().size());
            WorldGameplayCommitReport preparedReport;
            preparedReport.serverTick = phaseResult.ServerTick();
            preparedReport.pickupCount = static_cast<std::uint32_t>(phaseResult.Pickups().size());
            preparedReport.entityRemovals.reserve(phaseResult.PlayerDeaths().size() + phaseResult.Pickups().size());
            preparedReport.scoreSnapshots.reserve(phaseResult.PlayerDeaths().size() + phaseResult.BoostCosts().size() +
                                                  phaseResult.ScoreAwards().size());

            // gameplay phase 에서 반환한 playerDeath 검증
            const std::span<const WorldPlayerDeathCommit> playerDeaths = phaseResult.PlayerDeaths();
            for (std::size_t index = 0; index < playerDeaths.size(); ++index)
            {
                const WorldPlayerDeathCommit& death = playerDeaths[index];
                const std::uint8_t deathCauses = static_cast<std::uint8_t>(death.causes);
                const std::uint8_t validDeathCauses = static_cast<std::uint8_t>(
                    WorldPlayerDeathCauseFlags::Collision | WorldPlayerDeathCauseFlags::ActiveAreaBoundary);
                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, death.playerId);
                EntityHandle entityHandle;
                WorldEntityComponents components;
                if (death.playerId == 0 || !death.controlledEntityKey.IsValid() || deathCauses == 0 ||
                    (deathCauses & validDeathCauses) != deathCauses ||
                    (index > 0 && !(playerDeaths[index - 1].controlledEntityKey < death.controlledEntityKey)) ||
                    score == nullptr || score->controlledEntityKey != death.controlledEntityKey ||
                    score->lifecycle != WorldPlayerLifecycle::Alive ||
                    !entityManager.TryFindHandle(death.controlledEntityKey, &entityHandle) ||
                    !entityManager.TryReadComponents(entityHandle, &components) ||
                    components.replicationMetadata.entityKind != WorldEntityKind::Player ||
                    components.playerControl.playerId != death.playerId)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                for (const WorldPlayerBoostCostCommit& boostCost : phaseResult.BoostCosts())
                {
                    if (boostCost.playerId == death.playerId)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }
                for (const WorldPlayerScoreAward& award : phaseResult.ScoreAwards())
                {
                    if (award.playerId == death.playerId)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }
                for (const WorldResourcePickup& pickup : phaseResult.Pickups())
                {
                    if (pickup.playerId == death.playerId)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }

                // death 처리된 플레이어는
                // - pickup 이 없어야 하고, boost 가 없어야 하고, score 도 없어야 한다.
                preparedDeaths.push_back(PreparedPlayerDeath{death, entityHandle});
                preparedReport.entityRemovals.push_back(WorldGameplayEntityRemoval{
                    death.controlledEntityKey,
                    WorldGameplayEntityRemoveReason::PlayerDeath,
                });
                const std::vector<WorldGameplayScoreSnapshot>::iterator snapshot =
                    FindScoreSnapshot(preparedReport.scoreSnapshots, death.playerId);
                static_cast<void>(preparedReport.scoreSnapshots.insert(
                    snapshot, WorldGameplayScoreSnapshot{death.playerId, death.controlledEntityKey, 0}));
            }

            // boost 결과 검증
            const std::span<const WorldPlayerBoostCostCommit> boostCosts = phaseResult.BoostCosts();
            for (std::size_t index = 0; index < boostCosts.size(); ++index)
            {
                const WorldPlayerBoostCostCommit& boostCost = boostCosts[index];
                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, boostCost.playerId);
                if (boostCost.playerId == 0 || !boostCost.controlledEntityKey.IsValid() ||
                    (index > 0 && boostCosts[index - 1].playerId >= boostCost.playerId) || score == nullptr ||
                    score->controlledEntityKey != boostCost.controlledEntityKey ||
                    score->lifecycle != WorldPlayerLifecycle::Alive || boostCost.nextState.growthPoint > score->score ||
                    !std::isfinite(boostCost.nextState.boostCostAccumulator) ||
                    boostCost.nextState.boostCostAccumulator < 0.0f ||
                    (boostCost.nextState.growthPoint == score->score &&
                     boostCost.nextState.boostCostAccumulator == score->boostCostAccumulator))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                if (boostCost.nextState.growthPoint != score->score)
                {
                    const std::vector<WorldGameplayScoreSnapshot>::iterator snapshot =
                        FindScoreSnapshot(preparedReport.scoreSnapshots, boostCost.playerId);
                    if (snapshot != preparedReport.scoreSnapshots.end() && snapshot->playerId == boostCost.playerId)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                    static_cast<void>(
                        preparedReport.scoreSnapshots.insert(snapshot, WorldGameplayScoreSnapshot{
                                                                           boostCost.playerId,
                                                                           boostCost.controlledEntityKey,
                                                                           boostCost.nextState.growthPoint,
                                                                       }));
                }
            }

            // player의 resource pickup 정보 검증
            const std::span<const WorldResourcePickup> pickups = phaseResult.Pickups();
            for (std::size_t pickupIndex = 0; pickupIndex < pickups.size(); ++pickupIndex)
            {
                const WorldResourcePickup& pickup = pickups[pickupIndex];
                if (!pickup.resourceEntityKey.IsValid() || !pickup.resourceEntityHandle.IsValid() ||
                    pickup.playerId == 0 || !pickup.playerEntityKey.IsValid() || !pickup.playerEntityHandle.IsValid() ||
                    pickup.scoreAward != config.resourceScoreValue)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                if (pickupIndex > 0 && !(pickups[pickupIndex - 1].resourceEntityKey < pickup.resourceEntityKey))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                for (std::size_t previousIndex = 0; previousIndex < pickupIndex; ++previousIndex)
                {
                    if (pickup.resourceSlotId != 0 && pickups[previousIndex].resourceSlotId == pickup.resourceSlotId)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }

                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, pickup.playerId);
                WorldResourceInstance resourceInstance;
                if (score == nullptr || score->controlledEntityKey != pickup.playerEntityKey ||
                    score->lifecycle != WorldPlayerLifecycle::Alive ||
                    !gameplayState.resourceRegistry_.TryFind(pickup.resourceEntityKey, &resourceInstance) ||
                    resourceInstance.entityHandle != pickup.resourceEntityHandle ||
                    resourceInstance.ambientSlotId != pickup.resourceSlotId)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                if (resourceInstance.origin == WorldResourceOrigin::Ambient)
                {
                    WorldResourceSlotState* const slot =
                        FindResourceSlot(gameplayState.resourceSlots_, pickup.resourceSlotId);
                    if (slot == nullptr || slot->phase != WorldResourceSlotPhase::Active)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }
                else if (resourceInstance.origin != WorldResourceOrigin::DeathDrop || pickup.resourceSlotId != 0)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                EntityHandle currentResourceHandle;
                EntityHandle currentPlayerHandle;
                WorldEntityComponents resourceComponents;
                WorldEntityComponents playerComponents;
                if (!entityManager.TryFindHandle(pickup.resourceEntityKey, &currentResourceHandle) ||
                    currentResourceHandle != pickup.resourceEntityHandle ||
                    !entityManager.TryReadComponents(currentResourceHandle, &resourceComponents) ||
                    resourceComponents.replicationMetadata.entityKind != WorldEntityKind::Resource ||
                    !entityManager.TryFindHandle(pickup.playerEntityKey, &currentPlayerHandle) ||
                    currentPlayerHandle != pickup.playerEntityHandle ||
                    !entityManager.TryReadComponents(currentPlayerHandle, &playerComponents) ||
                    playerComponents.replicationMetadata.entityKind != WorldEntityKind::Player ||
                    playerComponents.playerControl.playerId != pickup.playerId)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                const WorldPlayerScoreAward* const award = FindScoreAward(phaseResult.ScoreAwards(), pickup.playerId);
                if (award == nullptr || award->controlledEntityKey != pickup.playerEntityKey)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                // 습득 이후에 제거(이후 다시 리스폰됨)
                // 제거 이유와 제거 대상 entity 기록
                preparedReport.entityRemovals.push_back(
                    WorldGameplayEntityRemoval{pickup.resourceEntityKey, WorldGameplayEntityRemoveReason::Collected});
            }

            // 점수 검증
            const std::span<const WorldPlayerScoreAward> scoreAwards = phaseResult.ScoreAwards();
            for (std::size_t awardIndex = 0; awardIndex < scoreAwards.size(); ++awardIndex)
            {
                const WorldPlayerScoreAward& award = scoreAwards[awardIndex];
                if (award.playerId == 0 || !award.controlledEntityKey.IsValid() || award.scoreAward == 0 ||
                    (awardIndex > 0 && scoreAwards[awardIndex - 1].playerId >= award.playerId))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                std::uint64_t pickupAwardSum = 0;
                for (const WorldResourcePickup& pickup : pickups)
                {
                    if (pickup.playerId == award.playerId)
                    {
                        if (pickup.playerEntityKey != award.controlledEntityKey)
                        {
                            return WorldGameplayCommitResult::StateInvariantViolation;
                        }
                        pickupAwardSum += pickup.scoreAward;
                    }
                }
                if (pickupAwardSum != award.scoreAward)
                {
                    return pickupAwardSum > std::numeric_limits<std::uint32_t>::max()
                               ? WorldGameplayCommitResult::ArithmeticOverflow
                               : WorldGameplayCommitResult::StateInvariantViolation;
                }

                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, award.playerId);
                if (score == nullptr || score->controlledEntityKey != award.controlledEntityKey ||
                    score->lifecycle != WorldPlayerLifecycle::Alive)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                std::vector<WorldGameplayScoreSnapshot>::iterator snapshot =
                    FindScoreSnapshot(preparedReport.scoreSnapshots, award.playerId);
                const std::uint32_t scoreBeforePickup =
                    snapshot != preparedReport.scoreSnapshots.end() && snapshot->playerId == award.playerId
                        ? snapshot->score
                        : score->score;
                if (scoreBeforePickup > std::numeric_limits<std::uint32_t>::max() - award.scoreAward)
                {
                    return WorldGameplayCommitResult::ArithmeticOverflow;
                }

                if (snapshot != preparedReport.scoreSnapshots.end() && snapshot->playerId == award.playerId)
                {
                    snapshot->score = scoreBeforePickup + award.scoreAward;
                }
                else
                {
                    static_cast<void>(preparedReport.scoreSnapshots.insert(
                        snapshot, WorldGameplayScoreSnapshot{award.playerId, award.controlledEntityKey,
                                                             scoreBeforePickup + award.scoreAward}));
                }
            }

            for (const PreparedPlayerDeath& preparedDeath : preparedDeaths)
            {
                WorldPlayerScore* const score = WorldPlayerScoreLookup::FindMutableByPlayerId(
                    gameplayState.playerScores_, preparedDeath.death.playerId);
                if (score == nullptr || !entityManager.Remove(preparedDeath.entityHandle))
                {
                    return WorldGameplayCommitResult::EntityRemoveFailed;
                }

                score->score = 0;
                score->boostCostAccumulator = 0.0f;
                score->lifecycle = WorldPlayerLifecycle::SpawnPending;
            }

            for (const WorldResourcePickup& pickup : pickups) // pickup 이후에 entity 제거 -> respawn 상태 전이
            {
                WorldResourceInstance resourceInstance;
                if (!gameplayState.resourceRegistry_.TryFind(pickup.resourceEntityKey, &resourceInstance) ||
                    !entityManager.Remove(pickup.resourceEntityHandle))
                {
                    return WorldGameplayCommitResult::EntityRemoveFailed;
                }
                if (gameplayState.resourceRegistry_.TryRemove(pickup.resourceEntityKey, pickup.resourceEntityHandle) !=
                    WorldResourceRemoveResult::Removed)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                if (resourceInstance.origin == WorldResourceOrigin::Ambient)
                {
                    WorldResourceSlotState* const slot =
                        FindResourceSlot(gameplayState.resourceSlots_, resourceInstance.ambientSlotId);
                    if (slot == nullptr)
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                    slot->phase = WorldResourceSlotPhase::Respawning;
                }
            }

            for (const WorldPlayerBoostCostCommit& boostCost : boostCosts)
            {
                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, boostCost.playerId);
                if (score == nullptr)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                score->score = boostCost.nextState.growthPoint;
                score->boostCostAccumulator = boostCost.nextState.boostCostAccumulator;
            }

            for (const WorldGameplayScoreSnapshot& snapshot : preparedReport.scoreSnapshots)
            {
                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, snapshot.playerId);
                if (score == nullptr)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                score->score = snapshot.score;
            }

            preparedReport.forceAoiReplication = !preparedReport.entityRemovals.empty();
            *outReport = std::move(preparedReport);
            return WorldGameplayCommitResult::Committed;
        }
        catch (const std::bad_alloc&)
        {
            return WorldGameplayCommitResult::AllocationFailed;
        }
    }

    // Compute 결과(phaseResult)를 Authoriative World 상태에 최종 반영하기 위한 Commit
    // round, 신규 생성 resource, 최종 commit
    WorldGameplayCommitResult WorldGameplayCommitter::Commit(const WorldGameplayConfig& config,
                                                             const WorldGameplayPhaseResult& phaseResult,
                                                             WorldEntityManager& entityManager,
                                                             WorldGameplayState& gameplayState,
                                                             WorldGameplayCommitReport* const outReport) noexcept
    {
        if (outReport == nullptr)
        {
            return WorldGameplayCommitResult::InvalidArgument;
        }
        if (phaseResult.DeathDropPlacementFailureCount() >
            std::numeric_limits<std::uint64_t>::max() - gameplayState.metrics_.deathDropPlacementFailureCount)
        {
            return WorldGameplayCommitResult::ArithmeticOverflow;
        }

        const WorldGameplayRoundTransition& transition = phaseResult.RoundTransition();
        const WorldGameplayRoundTransitionKind transitionKind = transition.kind;
        const bool resetsRound = transitionKind == WorldGameplayRoundTransitionKind::ResetToRunning ||
                                 transitionKind == WorldGameplayRoundTransitionKind::ResetToWaiting;
        const bool resetsScores = transitionKind == WorldGameplayRoundTransitionKind::Started || resetsRound;
        const bool changesRound = transitionKind != WorldGameplayRoundTransitionKind::None;

        if (resetsRound && gameplayState.roundState_.roundId == std::numeric_limits<std::uint32_t>::max())
        {
            return WorldGameplayCommitResult::ArithmeticOverflow;
        }

        // 상태 전이 논리 판단
        if ((transitionKind == WorldGameplayRoundTransitionKind::Started &&
             (gameplayState.roundState_.phase != WorldRoundPhase::Waiting ||
              transition.nextState.roundId != gameplayState.roundState_.roundId ||
              transition.nextState.phase != WorldRoundPhase::Running ||
              transition.nextState.phaseEndsAtServerTick == 0 || transition.nextState.winnerPlayerId != 0)) ||
            (transitionKind == WorldGameplayRoundTransitionKind::Ended &&
             (gameplayState.roundState_.phase != WorldRoundPhase::Running ||
              transition.nextState.roundId != gameplayState.roundState_.roundId ||
              transition.nextState.phase != WorldRoundPhase::Ended || transition.nextState.phaseEndsAtServerTick == 0 ||
              transition.nextState.winnerPlayerId != 0)) ||
            (transitionKind == WorldGameplayRoundTransitionKind::ResetToRunning &&
             (gameplayState.roundState_.phase != WorldRoundPhase::Ended ||
              transition.nextState.roundId != gameplayState.roundState_.roundId + 1 ||
              transition.nextState.phase != WorldRoundPhase::Running ||
              transition.nextState.phaseEndsAtServerTick == 0 || transition.nextState.winnerPlayerId != 0)) ||
            (transitionKind == WorldGameplayRoundTransitionKind::ResetToWaiting &&
             (gameplayState.roundState_.phase != WorldRoundPhase::Ended ||
              transition.nextState.roundId != gameplayState.roundState_.roundId + 1 ||
              transition.nextState.phase != WorldRoundPhase::Waiting ||
              transition.nextState.phaseEndsAtServerTick != 0 || transition.nextState.winnerPlayerId != 0)) ||
            transitionKind > WorldGameplayRoundTransitionKind::ResetToWaiting)
        {
            return WorldGameplayCommitResult::StateInvariantViolation;
        }

        // 생성할 Resource Entity 검증
        if ((transitionKind == WorldGameplayRoundTransitionKind::Ended ||
             transitionKind == WorldGameplayRoundTransitionKind::ResetToWaiting) &&
            !phaseResult.ResourceSpawns().empty())
        {
            return WorldGameplayCommitResult::StateInvariantViolation;
        }
        if (resetsRound && (!phaseResult.Pickups().empty() || !phaseResult.DeathDropSpawns().empty()))
        {
            return WorldGameplayCommitResult::StateInvariantViolation;
        }
        if (!phaseResult.DisconnectDrops().empty() &&
            (gameplayState.roundState_.phase != WorldRoundPhase::Running || resetsRound))
        {
            return WorldGameplayCommitResult::StateInvariantViolation;
        }
        const std::span<const WorldDisconnectDropCommit> disconnectDrops = phaseResult.DisconnectDrops();
        for (std::size_t index = 0; index < disconnectDrops.size(); ++index)
        {
            const WorldDisconnectDropCommit& disconnectDrop = disconnectDrops[index];
            WorldPlayerScore currentScore;
            EntityHandle currentHandle;
            if (disconnectDrop.playerId == 0 || !disconnectDrop.sourceEntityKey.IsValid() ||
                (index > 0 && !(disconnectDrops[index - 1].sourceEntityKey < disconnectDrop.sourceEntityKey)) ||
                gameplayState.TryFindPlayerScore(disconnectDrop.playerId, &currentScore) ||
                entityManager.TryFindHandle(disconnectDrop.sourceEntityKey, &currentHandle))
            {
                return WorldGameplayCommitResult::StateInvariantViolation;
            }
        }
        if (!phaseResult.OutsideResourceRemovals().empty() &&
            (gameplayState.roundState_.phase != WorldRoundPhase::Running || resetsRound))
        {
            return WorldGameplayCommitResult::StateInvariantViolation;
        }
        const std::span<const WorldResourceInstance> outsideResourceRemovals = phaseResult.OutsideResourceRemovals();
        for (std::size_t index = 0; index < outsideResourceRemovals.size(); ++index)
        {
            const WorldResourceInstance& resource = outsideResourceRemovals[index];
            WorldResourceInstance currentResource;
            EntityHandle currentHandle;
            WorldEntityComponents components;
            if (index > 0 && !(outsideResourceRemovals[index - 1].entityKey < resource.entityKey))
            {
                return WorldGameplayCommitResult::StateInvariantViolation;
            }
            for (const WorldResourcePickup& pickup : phaseResult.Pickups())
            {
                if (pickup.resourceEntityKey == resource.entityKey)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
            }
            if (!gameplayState.resourceRegistry_.TryFind(resource.entityKey, &currentResource) ||
                currentResource != resource || !entityManager.TryFindHandle(resource.entityKey, &currentHandle) ||
                currentHandle != resource.entityHandle ||
                !entityManager.TryReadComponents(resource.entityHandle, &components) ||
                components.replicationMetadata.entityKind != WorldEntityKind::Resource)
            {
                return WorldGameplayCommitResult::StateInvariantViolation;
            }
            if (resource.origin == WorldResourceOrigin::Ambient)
            {
                WorldResourceSlotState* const slot =
                    FindResourceSlot(gameplayState.resourceSlots_, resource.ambientSlotId);
                if (slot == nullptr || slot->phase != WorldResourceSlotPhase::Active)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
            }
            else if (resource.origin != WorldResourceOrigin::DeathDrop || resource.ambientSlotId != 0)
            {
                return WorldGameplayCommitResult::StateInvariantViolation;
            }
        }
        if ((transitionKind == WorldGameplayRoundTransitionKind::Started ||
             transitionKind == WorldGameplayRoundTransitionKind::ResetToRunning) &&
            phaseResult.ResourceSpawns().size() != gameplayState.resourceSlots_.size())
        {
            return WorldGameplayCommitResult::StateInvariantViolation;
        }
        if (phaseResult.ResourceSpawns().size() > std::numeric_limits<std::uint32_t>::max())
        {
            return WorldGameplayCommitResult::ArithmeticOverflow;
        }
        if (phaseResult.DeathDropSpawns().size() >
            std::numeric_limits<std::uint32_t>::max() - phaseResult.ResourceSpawns().size())
        {
            return WorldGameplayCommitResult::ArithmeticOverflow;
        }

        // 리소스 생성
        std::vector<CreatedResource> created;
        try
        {
            const std::size_t resourceSpawnCount =
                phaseResult.ResourceSpawns().size() + phaseResult.DeathDropSpawns().size();
            created.reserve(resourceSpawnCount);
            std::vector<WorldResourceInstance> resetResources;
            if (resetsRound)
            {
                const std::span<const WorldResourceInstance> activeResources =
                    gameplayState.resourceRegistry_.Instances();
                resetResources.assign(activeResources.begin(), activeResources.end());
            }
            for (std::size_t index = 0; index < phaseResult.ResourceSpawns().size(); ++index)
            {
                const WorldResourceSpawnRequest& request = phaseResult.ResourceSpawns()[index];
                if (request.origin != WorldResourceOrigin::Ambient || request.ambientSlotId == 0 ||
                    request.sourceEntityKey.IsValid() ||
                    (index > 0 && phaseResult.ResourceSpawns()[index - 1].ambientSlotId >= request.ambientSlotId))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                WorldResourceSlotState* const slot =
                    FindResourceSlot(gameplayState.resourceSlots_, request.ambientSlotId);
                if (slot == nullptr || !std::isfinite(request.positionX) || !std::isfinite(request.positionY))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                if (transitionKind == WorldGameplayRoundTransitionKind::Started &&
                    slot->phase != WorldResourceSlotPhase::Dormant)
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                const bool replacesPickup =
                    WorldResourcePickup::ContainsAmbientPickupForSlot(phaseResult.Pickups(), request.ambientSlotId);
                const bool replacesOutsideResource = WorldResourceInstance::ContainsAmbientSlot(
                    phaseResult.OutsideResourceRemovals(), request.ambientSlotId);
                if (transitionKind == WorldGameplayRoundTransitionKind::None &&
                    (gameplayState.roundState_.phase != WorldRoundPhase::Running ||
                     (slot->phase != WorldResourceSlotPhase::Respawning &&
                      !(slot->phase == WorldResourceSlotPhase::Active && (replacesPickup || replacesOutsideResource)))))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
            }

            const std::span<const WorldResourceSpawnRequest> deathDropSpawns = phaseResult.DeathDropSpawns();
            for (std::size_t index = 0; index < deathDropSpawns.size(); ++index)
            {
                const WorldResourceSpawnRequest& request = deathDropSpawns[index];
                const WorldPlayerDeathCommit* const death =
                    FindPlayerDeath(phaseResult.PlayerDeaths(), request.sourceEntityKey);
                const WorldDisconnectDropCommit* const disconnectDrop =
                    FindDisconnectDrop(disconnectDrops, request.sourceEntityKey);
                WorldPlayerScore* sourceScore = nullptr;
                if (death != nullptr)
                {
                    sourceScore =
                        WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, death->playerId);
                }
                const bool validPlayerDeath = death != nullptr && disconnectDrop == nullptr &&
                                              death->causes == WorldPlayerDeathCauseFlags::Collision &&
                                              sourceScore != nullptr &&
                                              sourceScore->controlledEntityKey == request.sourceEntityKey &&
                                              request.sourceOrdinal < sourceScore->score;
                const bool validDisconnectDrop = death == nullptr && disconnectDrop != nullptr &&
                                                 request.sourceOrdinal < disconnectDrop->growthPoint;
                if (request.origin != WorldResourceOrigin::DeathDrop || request.ambientSlotId != 0 ||
                    !request.sourceEntityKey.IsValid() || !std::isfinite(request.positionX) ||
                    !std::isfinite(request.positionY) || (!validPlayerDeath && !validDisconnectDrop))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
                if (index > 0)
                {
                    const WorldResourceSpawnRequest& previous = deathDropSpawns[index - 1];
                    if (request.sourceEntityKey < previous.sourceEntityKey ||
                        (request.sourceEntityKey == previous.sourceEntityKey &&
                         request.sourceOrdinal <= previous.sourceOrdinal))
                    {
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }
            }

            for (const WorldResourceSpawnRequest& request : deathDropSpawns)
            {
                const WorldGameplayCommitResult createResult = CreateResource(config, request, entityManager, &created);
                if (createResult != WorldGameplayCommitResult::Committed)
                {
                    if (!RollbackCreatedResources(created, entityManager))
                    {
                        return WorldGameplayCommitResult::RollbackFailed;
                    }
                    return createResult;
                }
            }
            for (const WorldResourceSpawnRequest& request : phaseResult.ResourceSpawns())
            {
                const WorldGameplayCommitResult createResult = CreateResource(config, request, entityManager, &created);
                if (createResult != WorldGameplayCommitResult::Committed)
                {
                    if (!RollbackCreatedResources(created, entityManager))
                    {
                        return WorldGameplayCommitResult::RollbackFailed;
                    }
                    return createResult;
                }
            }

            WorldResourceRegistry preparedResourceRegistry = gameplayState.resourceRegistry_;
            const WorldResourceReserveResult reserveResult = preparedResourceRegistry.ReserveAdditional(created.size());
            if (reserveResult != WorldResourceReserveResult::Reserved)
            {
                if (!RollbackCreatedResources(created, entityManager))
                {
                    return WorldGameplayCommitResult::RollbackFailed;
                }
                if (reserveResult == WorldResourceReserveResult::CapacityExceeded)
                {
                    return WorldGameplayCommitResult::ArithmeticOverflow;
                }
                return WorldGameplayCommitResult::AllocationFailed;
            }
            for (const WorldResourcePickup& pickup : phaseResult.Pickups())
            {
                if (preparedResourceRegistry.TryRemove(pickup.resourceEntityKey, pickup.resourceEntityHandle) !=
                    WorldResourceRemoveResult::Removed)
                {
                    if (!RollbackCreatedResources(created, entityManager))
                    {
                        return WorldGameplayCommitResult::RollbackFailed;
                    }
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
            }
            for (const WorldResourceInstance& resource : outsideResourceRemovals)
            {
                if (preparedResourceRegistry.TryRemove(resource.entityKey, resource.entityHandle) !=
                    WorldResourceRemoveResult::Removed)
                {
                    if (!RollbackCreatedResources(created, entityManager))
                    {
                        return WorldGameplayCommitResult::RollbackFailed;
                    }
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
            }
            if (resetsRound)
            {
                for (const WorldResourceInstance& resourceInstance : resetResources)
                {
                    if (preparedResourceRegistry.TryRemove(resourceInstance.entityKey, resourceInstance.entityHandle) !=
                        WorldResourceRemoveResult::Removed)
                    {
                        if (!RollbackCreatedResources(created, entityManager))
                        {
                            return WorldGameplayCommitResult::RollbackFailed;
                        }
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                }
            }
            for (const CreatedResource& resource : created)
            {
                const WorldResourceRegisterResult registerResult = preparedResourceRegistry.TryRegister(
                    WorldResourceInstance{resource.entityKey, resource.entityHandle, resource.origin,
                                          resource.ambientSlotId, resource.positionX, resource.positionY});
                if (registerResult != WorldResourceRegisterResult::Registered)
                {
                    if (!RollbackCreatedResources(created, entityManager))
                    {
                        return WorldGameplayCommitResult::RollbackFailed;
                    }
                    if (registerResult == WorldResourceRegisterResult::AllocationFailed)
                    {
                        return WorldGameplayCommitResult::AllocationFailed;
                    }
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }
            }

            WorldGameplayCommitReport report;
            const WorldGameplayCommitResult pickupCommitResult =
                CommitPickups(config, phaseResult, entityManager, gameplayState, &report);
            if (pickupCommitResult != WorldGameplayCommitResult::Committed)
            {
                return RollbackCreatedResources(created, entityManager) ? pickupCommitResult
                                                                        : WorldGameplayCommitResult::RollbackFailed;
            }

            report.entityRemovals.reserve(report.entityRemovals.size() + outsideResourceRemovals.size() +
                                          resetResources.size());
            report.spawnedResourceKeys.reserve(created.size());
            report.scoreSnapshots.reserve(report.scoreSnapshots.size() +
                                          (resetsScores ? gameplayState.playerScores_.size() : 0));

            for (const WorldResourceInstance& resource : outsideResourceRemovals)
            {
                if (!entityManager.Remove(resource.entityHandle) ||
                    gameplayState.resourceRegistry_.TryRemove(resource.entityKey, resource.entityHandle) !=
                        WorldResourceRemoveResult::Removed)
                {
                    static_cast<void>(RollbackCreatedResources(created, entityManager));
                    return WorldGameplayCommitResult::EntityRemoveFailed;
                }
                if (resource.origin == WorldResourceOrigin::Ambient)
                {
                    WorldResourceSlotState* const slot =
                        FindResourceSlot(gameplayState.resourceSlots_, resource.ambientSlotId);
                    if (slot == nullptr)
                    {
                        static_cast<void>(RollbackCreatedResources(created, entityManager));
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                    slot->phase = WorldResourceSlotPhase::Respawning;
                }
                report.entityRemovals.push_back(WorldGameplayEntityRemoval{
                    resource.entityKey,
                    WorldGameplayEntityRemoveReason::OutsideActiveArea,
                });
            }
            report.forceAoiReplication = report.forceAoiReplication || !outsideResourceRemovals.empty();

            // Round Reset 처리
            if (resetsRound)
            {
                for (const WorldResourceInstance& resourceInstance : resetResources)
                {
                    if (!entityManager.Remove(resourceInstance.entityHandle))
                    {
                        static_cast<void>(RollbackCreatedResources(created, entityManager));
                        return WorldGameplayCommitResult::EntityRemoveFailed;
                    }
                    if (gameplayState.resourceRegistry_.TryRemove(resourceInstance.entityKey,
                                                                  resourceInstance.entityHandle) !=
                        WorldResourceRemoveResult::Removed)
                    {
                        static_cast<void>(RollbackCreatedResources(created, entityManager));
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                    report.entityRemovals.push_back(WorldGameplayEntityRemoval{
                        resourceInstance.entityKey,
                        WorldGameplayEntityRemoveReason::RoundReset,
                    });
                }
                for (WorldResourceSlotState& slot : gameplayState.resourceSlots_)
                {
                    slot.phase = WorldResourceSlotPhase::Dormant;
                }
            }

            // 새 resource 를 slot 에 확정
            for (const CreatedResource& resource : created)
            {
                if (resource.origin == WorldResourceOrigin::Ambient)
                {
                    WorldResourceSlotState* const slot =
                        FindResourceSlot(gameplayState.resourceSlots_, resource.ambientSlotId);
                    if (slot == nullptr)
                    {
                        static_cast<void>(RollbackCreatedResources(created, entityManager));
                        return WorldGameplayCommitResult::StateInvariantViolation;
                    }
                    slot->phase = WorldResourceSlotPhase::Active;
                }
                report.spawnedResourceKeys.push_back(resource.entityKey);
            }
            gameplayState.resourceRegistry_ = std::move(preparedResourceRegistry);

            if (resetsScores) // 점수 초기화 필요시
            {
                for (WorldPlayerScore& score : gameplayState.playerScores_)
                {
                    score.score = 0;
                    score.boostCostAccumulator = 0.0f;
                    report.scoreSnapshots.push_back(
                        WorldGameplayScoreSnapshot{score.playerId, score.controlledEntityKey, 0});
                }
                std::sort(report.scoreSnapshots.begin(), report.scoreSnapshots.end(),
                          [](const WorldGameplayScoreSnapshot& left, const WorldGameplayScoreSnapshot& right) noexcept
                          { return left.playerId < right.playerId; });
            }

            if (changesRound) // 라운드 변경시
            {
                gameplayState.roundState_ = transition.nextState;
                report.roundSnapshotChanged = true;
                report.roundSnapshot = transition.nextState;
            }

            gameplayState.metrics_.deathDropPlacementFailureCount += phaseResult.DeathDropPlacementFailureCount();

            report.resourceSpawnCount = static_cast<std::uint32_t>(created.size());
            report.forceAoiReplication = !report.entityRemovals.empty() || !report.spawnedResourceKeys.empty();
            *outReport = std::move(report);
            return WorldGameplayCommitResult::Committed;
        }
        catch (const std::bad_alloc&)
        {
            return RollbackCreatedResources(created, entityManager) ? WorldGameplayCommitResult::AllocationFailed
                                                                    : WorldGameplayCommitResult::RollbackFailed;
        }
    }

    WorldGameplayCommitResult WorldGameplayCommitter::CommitPlayerSpawns(
        const std::span<const WorldPlayerSpawnCandidate> candidates, WorldEntityManager& entityManager,
        WorldGameplayState& gameplayState, WorldSessionRegistry& sessionRegistry,
        WorldGameplayCommitReport* const inOutReport) noexcept
    {
        if (inOutReport == nullptr)
        {
            return WorldGameplayCommitResult::InvalidArgument;
        }
        if (candidates.empty())
        {
            return WorldGameplayCommitResult::Committed;
        }

        std::vector<PreparedPlayerSpawn> prepared;
        std::vector<CreatedPlayer> created;
        std::size_t reboundSessionCount = 0;
        try
        {
            prepared.reserve(candidates.size());
            created.reserve(candidates.size());
            inOutReport->playerSpawns.reserve(inOutReport->playerSpawns.size() + candidates.size());

            const std::span<const WorldSession> joinedSessions = sessionRegistry.JoinedSessions();
            for (std::size_t index = 0; index < candidates.size(); ++index)
            {
                const WorldPlayerSpawnCandidate& candidate = candidates[index];

                WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindMutableByPlayerId(gameplayState.playerScores_, candidate.playerId);

                // 활성화된 session 확인
                // 이후 entity 를 해당 session에 bind함
                const WorldSession* const session =
                    WorldSessionLookup::FindUniqueByPlayerId(joinedSessions, candidate.playerId);

                EntityHandle staleEntityHandle;
                if (!IsValid(candidate) || (index > 0 && candidates[index - 1].playerId >= candidate.playerId) ||
                    score == nullptr || score->lifecycle != WorldPlayerLifecycle::SpawnPending || score->score != 0 ||
                    score->boostCostAccumulator != 0.0f || !score->controlledEntityKey.IsValid() ||
                    session == nullptr || !session->IsJoined() || session->entityKey != score->controlledEntityKey ||
                    entityManager.TryFindHandle(score->controlledEntityKey, &staleEntityHandle))
                {
                    return WorldGameplayCommitResult::StateInvariantViolation;
                }

                prepared.push_back(
                    PreparedPlayerSpawn{&candidate, score, session->sessionKey, score->controlledEntityKey});
            }

            for (const PreparedPlayerSpawn& spawn : prepared)
            {
                WorldEntityKey spawnedEntityKey;
                EntityHandle spawnedEntityHandle;
                if (!entityManager.TryCreate(spawn.candidate->components, &spawnedEntityKey, &spawnedEntityHandle))
                {
                    return RollbackCreatedPlayers(created, reboundSessionCount, entityManager, sessionRegistry)
                               ? WorldGameplayCommitResult::EntityCreateFailed
                               : WorldGameplayCommitResult::RollbackFailed;
                }

                created.push_back(CreatedPlayer{
                    spawn.candidate->playerId,
                    spawn.sessionKey,
                    spawn.previousEntityKey,
                    spawnedEntityKey,
                    spawnedEntityHandle,
                    spawn.score,
                });
            }

            for (const CreatedPlayer& player : created)
            {
                // 신규 entityKey 로 바인딩
                if (!sessionRegistry.TryRebindControlledEntity(player.sessionKey, player.spawnedEntityKey))
                {
                    return RollbackCreatedPlayers(created, reboundSessionCount, entityManager, sessionRegistry)
                               ? WorldGameplayCommitResult::StateInvariantViolation
                               : WorldGameplayCommitResult::RollbackFailed;
                }
                ++reboundSessionCount;
            }

            for (const CreatedPlayer& player : created)
            {
                player.score->controlledEntityKey = player.spawnedEntityKey;
                player.score->lifecycle = WorldPlayerLifecycle::Alive;
                const std::vector<WorldGameplayScoreSnapshot>::iterator scoreSnapshot =
                    FindScoreSnapshot(inOutReport->scoreSnapshots, player.playerId);
                if (scoreSnapshot != inOutReport->scoreSnapshots.end() && scoreSnapshot->playerId == player.playerId)
                {
                    scoreSnapshot->controlledEntityKey = player.spawnedEntityKey;
                }

                inOutReport->playerSpawns.push_back(WorldGameplayPlayerSpawn{
                    player.playerId,
                    player.sessionKey,
                    player.previousEntityKey,
                    player.spawnedEntityKey,
                });
            }

            inOutReport->forceAoiReplication = true;
            return WorldGameplayCommitResult::Committed;
        }
        catch (...)
        {
            return RollbackCreatedPlayers(created, reboundSessionCount, entityManager, sessionRegistry)
                       ? WorldGameplayCommitResult::AllocationFailed
                       : WorldGameplayCommitResult::RollbackFailed;
        }
    }
} // namespace psnr::world
