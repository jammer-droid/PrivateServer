#include "pch.h"

#include "WorldGameplayPhase.h"

#include "WorldResourcePopulationSolver.h"
#include "WorldResourceSpawnPlanner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        struct ClassifiedEntity final
        {
            WorldEntityKey key{};
            EntityHandle handle{};
            PhysicsFixtureId fixtureId{};
            WorldEntityComponents components{};
        };

        [[nodiscard]] const WorldResourceSlotState* FindResourceSlot(
            const std::span<const WorldResourceSlotState> resourceSlots, const std::uint32_t slotId) noexcept
        {
            const std::span<const WorldResourceSlotState>::iterator found =
                std::lower_bound(resourceSlots.begin(), resourceSlots.end(), slotId,
                                 [](const WorldResourceSlotState& slot, const std::uint32_t expectedSlotId) noexcept
                                 { return slot.slotId < expectedSlotId; });
            if (found == resourceSlots.end() || found->slotId != slotId)
            {
                return nullptr;
            }
            return &*found;
        }

        [[nodiscard]] bool PickupLess(const WorldResourcePickup& left, const WorldResourcePickup& right) noexcept
        {
            if (left.resourceEntityKey != right.resourceEntityKey)
            {
                return left.resourceEntityKey < right.resourceEntityKey;
            }
            if (left.playerEntityKey != right.playerEntityKey)
            {
                return left.playerEntityKey < right.playerEntityKey;
            }
            if (left.playerFixtureId.value != right.playerFixtureId.value)
            {
                return left.playerFixtureId.value < right.playerFixtureId.value;
            }

            return left.resourceFixtureId.value < right.resourceFixtureId.value;
        }

        [[nodiscard]] WorldResult<void> ClassifyPickup(const WorldTriggerOverlap& overlap,
                                                       const WorldReadView& readView,
                                                       const std::span<const WorldPlayerScore> playerScores,
                                                       const std::span<const WorldResourceSlotState> resourceSlots,
                                                       const WorldResourceRegistry& resourceRegistry,
                                                       const std::uint32_t scoreAward,
                                                       WorldResourcePickup* const outPickup, bool* const outIsPickup)
        {
            if (outPickup == nullptr || outIsPickup == nullptr || !overlap.first.ownerKey.IsValid() ||
                !overlap.second.ownerKey.IsValid() || !overlap.first.fixtureId.IsValid() ||
                !overlap.second.fixtureId.IsValid())
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
            }

            ClassifiedEntity first;
            first.key = overlap.first.ownerKey;
            first.fixtureId = overlap.first.fixtureId;
            ClassifiedEntity second;
            second.key = overlap.second.ownerKey;
            second.fixtureId = overlap.second.fixtureId;
            if (!readView.TryReadEntity(first.key, &first.handle, &first.components) ||
                !readView.TryReadEntity(second.key, &second.handle, &second.components))
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
            }

            const ClassifiedEntity* player = nullptr;
            const ClassifiedEntity* resource = nullptr;
            if (first.components.replicationMetadata.entityKind == WorldEntityKind::Player &&
                second.components.replicationMetadata.entityKind == WorldEntityKind::Resource)
            {
                player = &first;
                resource = &second;
            }
            else if (first.components.replicationMetadata.entityKind == WorldEntityKind::Resource &&
                     second.components.replicationMetadata.entityKind == WorldEntityKind::Player)
            {
                player = &second;
                resource = &first;
            }
            else
            {
                *outIsPickup = false;
                return WorldResult<void>::Success();
            }

            const std::uint32_t playerId = player->components.playerControl.playerId;
            const WorldPlayerScore* const playerScore = WorldPlayerScoreLookup::FindByPlayerId(playerScores, playerId);
            WorldResourceInstance resourceInstance;
            if (!resourceRegistry.TryFind(resource->key, &resourceInstance))
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
            }
            const WorldResourceSlotState* resourceSlot = nullptr;
            if (resourceInstance.origin == WorldResourceOrigin::Ambient)
            {
                resourceSlot = FindResourceSlot(resourceSlots, resourceInstance.ambientSlotId);
                if (resourceSlot == nullptr || resourceSlot->phase != WorldResourceSlotPhase::Active)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
                }
            }
            else if (resourceInstance.origin != WorldResourceOrigin::DeathDrop || resourceInstance.ambientSlotId != 0)
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
            }
            if (playerId == 0 || playerScore == nullptr || playerScore->controlledEntityKey != player->key ||
                resourceInstance.entityHandle != resource->handle || resource->components.playerControl.playerId != 0)
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
            }
            if (playerScore->lifecycle == WorldPlayerLifecycle::SpawnPending)
            {
                *outIsPickup = false;
                return WorldResult<void>::Success();
            }
            if (playerScore->lifecycle != WorldPlayerLifecycle::Alive)
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
            }

            *outPickup = WorldResourcePickup{
                resourceInstance.ambientSlotId,
                resource->key,
                resource->handle,
                resource->fixtureId,
                playerId,
                player->key,
                player->handle,
                player->fixtureId,
                scoreAward,
            };
            *outIsPickup = true;
            return WorldResult<void>::Success();
        }

        [[nodiscard]] bool TryAddTicks(const std::uint32_t serverTick, const std::uint32_t durationTicks,
                                       std::uint32_t* const outDeadline) noexcept
        {
            if (outDeadline == nullptr || serverTick > std::numeric_limits<std::uint32_t>::max() - durationTicks)
            {
                return false;
            }

            *outDeadline = serverTick + durationTicks;
            return true;
        }

        [[nodiscard]] WorldResult<void> TryBuildAllResourceSpawns(
            const std::span<const WorldResourceSlotState> resourceSlots, const WorldResourceRegistry& resourceRegistry,
            const WorldActiveArea& activeArea, const float resourceCircleRadius, const std::uint32_t serverTick,
            std::vector<WorldResourceSpawnRequest>* const outSpawns)
        {
            if (outSpawns == nullptr)
            {
                return WorldResult<void>::Failure(WorldErrorCode::InvalidArgument);
            }
            outSpawns->reserve(resourceSlots.size());
            std::vector<WorldResourcePosition> reservedPositions;
            reservedPositions.reserve(resourceSlots.size());
            for (std::size_t index = 0; index < resourceSlots.size(); ++index)
            {
                const WorldResourceSlotState& slot = resourceSlots[index];
                if (slot.slotId == 0 || (index > 0 && resourceSlots[index - 1].slotId >= slot.slotId))
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
                }

                WorldResult<WorldResourceSpawnPlan> planningResult = WorldResourceSpawnPlanner::PlanAmbient(
                    activeArea, resourceCircleRadius, serverTick, slot.slotId, resourceRegistry, reservedPositions);
                if (planningResult.Failed() && planningResult.Error() == WorldErrorCode::AllocationFailed)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::AllocationFailed);
                }
                if (planningResult.Failed())
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
                }
                WorldResourceSpawnPlan spawnPlan = planningResult.TakeValue();
                if (spawnPlan.requests.size() != 1 || spawnPlan.rejectedCount != 0)
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
                }
                const WorldResourceSpawnRequest& spawn = spawnPlan.requests[0];
                if (spawn.origin != WorldResourceOrigin::Ambient || spawn.ambientSlotId != slot.slotId ||
                    spawn.sourceEntityKey.IsValid())
                {
                    return WorldResult<void>::Failure(WorldErrorCode::InvalidState);
                }
                outSpawns->push_back(spawn);
                reservedPositions.push_back(WorldResourcePosition{spawn.positionX, spawn.positionY});
            }
            return WorldResult<void>::Success();
        }

    } // namespace

    WorldResult<WorldGameplayPhaseResult> WorldGameplayPhase::Compute(
        const std::uint32_t serverTick, const WorldGameplayConfig& config, const WorldPhysicsStepResult& physicsResult,
        const WorldReadView& postMovementReadView, const WorldRoundRuntimeState& roundState,
        const std::span<const WorldPlayerScore> playerScores,
        const std::span<const WorldResourceSlotState> resourceSlots, const WorldResourceRegistry& resourceRegistry,
        const std::span<const WorldEntityKey> collisionDeathSet,
        const std::span<const WorldEntityKey> activeAreaBoundaryDeathSet, const float fixedDeltaSeconds,
        const WorldActiveArea* const activeArea) noexcept
    {
        return Compute(serverTick, config, physicsResult, postMovementReadView, roundState, playerScores, resourceSlots,
                       resourceRegistry, collisionDeathSet, activeAreaBoundaryDeathSet, {}, fixedDeltaSeconds,
                       activeArea);
    }

    WorldResult<WorldGameplayPhaseResult> WorldGameplayPhase::Compute(
        const std::uint32_t serverTick, const WorldGameplayConfig& config, const WorldPhysicsStepResult& physicsResult,
        const WorldReadView& postMovementReadView, const WorldRoundRuntimeState& roundState,
        const std::span<const WorldPlayerScore> playerScores,
        const std::span<const WorldResourceSlotState> resourceSlots, const WorldResourceRegistry& resourceRegistry,
        const std::span<const WorldEntityKey> collisionDeathSet,
        const std::span<const WorldEntityKey> activeAreaBoundaryDeathSet,
        const std::span<const WorldDisconnectDropSnapshot> disconnectDropSnapshots, const float fixedDeltaSeconds,
        const WorldActiveArea* const activeArea) noexcept
    {
        if (config.minimumPlayersToStart == 0 || config.scoreToWin == 0 || config.roundDurationTicks == 0 ||
            config.endedDurationTicks == 0 || !std::isfinite(config.activeAreaStartRatio) ||
            !std::isfinite(config.activeAreaEndRatio) || config.activeAreaEndRatio <= 0.0f ||
            config.activeAreaEndRatio > config.activeAreaStartRatio || config.activeAreaStartRatio > 1.0f ||
            (!WorldBoostCostSolver::IsDisabled(config.boostCost) &&
             (!WorldBoostCostSolver::IsValidConfig(config.boostCost) || !std::isfinite(fixedDeltaSeconds) ||
              fixedDeltaSeconds <= 0.0f)))
        {
            return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidConfig);
        }
        try
        {
            if (roundState.phase == WorldRoundPhase::Waiting)
            {
                if (playerScores.size() < config.minimumPlayersToStart)
                {
                    return WorldResult<WorldGameplayPhaseResult>(WorldGameplayPhaseResult{serverTick, {}, {}});
                }

                std::uint32_t runningDeadline = 0;
                if (!TryAddTicks(serverTick, config.roundDurationTicks, &runningDeadline))
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::ArithmeticOverflow);
                }
                std::vector<WorldResourceSpawnRequest> resourceSpawns;
                if (activeArea == nullptr || !activeArea->IsValid())
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }
                const WorldResult<void> spawnBuildResult =
                    TryBuildAllResourceSpawns(resourceSlots, resourceRegistry, *activeArea, config.resourceCircleRadius,
                                              serverTick, &resourceSpawns);
                if (spawnBuildResult.Failed())
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(spawnBuildResult.Error());
                }

                return WorldResult<WorldGameplayPhaseResult>(WorldGameplayPhaseResult{
                    serverTick,
                    {},
                    {},
                    std::move(resourceSpawns),
                    WorldGameplayRoundTransition{
                        WorldGameplayRoundTransitionKind::Started,
                        WorldRoundRuntimeState{roundState.roundId, WorldRoundPhase::Running, runningDeadline, 0},
                    },
                });
            }

            if (roundState.phase == WorldRoundPhase::Ended)
            {
                return WorldResult<WorldGameplayPhaseResult>(WorldGameplayPhaseResult{serverTick, {}, {}});
            }

            if (roundState.phase != WorldRoundPhase::Running)
            {
                return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
            }
            if (activeArea == nullptr || !activeArea->IsValid())
            {
                return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
            }
            std::vector<WorldPlayerScore> projectedScores(playerScores.begin(), playerScores.end());
            std::vector<WorldResourcePosition> reservedResourceSpawnPositions;
            std::vector<WorldResourceSpawnRequest> deathDropSpawns;
            std::uint32_t deathDropPlacementFailureCount = 0;
            std::vector<WorldPlayerDeathCommit> playerDeaths;
            playerDeaths.reserve(collisionDeathSet.size() + activeAreaBoundaryDeathSet.size());
            std::size_t collisionDeathIndex = 0;
            std::size_t activeAreaBoundaryDeathIndex = 0;
            while (collisionDeathIndex < collisionDeathSet.size() ||
                   activeAreaBoundaryDeathIndex < activeAreaBoundaryDeathSet.size())
            {
                if ((collisionDeathIndex < collisionDeathSet.size() &&
                     (!collisionDeathSet[collisionDeathIndex].IsValid() ||
                      (collisionDeathIndex > 0 &&
                       !(collisionDeathSet[collisionDeathIndex - 1] < collisionDeathSet[collisionDeathIndex])))) ||
                    (activeAreaBoundaryDeathIndex < activeAreaBoundaryDeathSet.size() &&
                     (!activeAreaBoundaryDeathSet[activeAreaBoundaryDeathIndex].IsValid() ||
                      (activeAreaBoundaryDeathIndex > 0 &&
                       !(activeAreaBoundaryDeathSet[activeAreaBoundaryDeathIndex - 1] <
                         activeAreaBoundaryDeathSet[activeAreaBoundaryDeathIndex])))))
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }

                WorldEntityKey entityKey;
                if (activeAreaBoundaryDeathIndex >= activeAreaBoundaryDeathSet.size() ||
                    (collisionDeathIndex < collisionDeathSet.size() &&
                     collisionDeathSet[collisionDeathIndex] < activeAreaBoundaryDeathSet[activeAreaBoundaryDeathIndex]))
                {
                    entityKey = collisionDeathSet[collisionDeathIndex];
                }
                else
                {
                    entityKey = activeAreaBoundaryDeathSet[activeAreaBoundaryDeathIndex];
                }

                WorldPlayerDeathCauseFlags causes = WorldPlayerDeathCauseFlags::None;
                if (collisionDeathIndex < collisionDeathSet.size() &&
                    collisionDeathSet[collisionDeathIndex] == entityKey)
                {
                    causes = causes | WorldPlayerDeathCauseFlags::Collision;
                    ++collisionDeathIndex;
                }
                if (activeAreaBoundaryDeathIndex < activeAreaBoundaryDeathSet.size() &&
                    activeAreaBoundaryDeathSet[activeAreaBoundaryDeathIndex] == entityKey)
                {
                    causes = causes | WorldPlayerDeathCauseFlags::ActiveAreaBoundary;
                    ++activeAreaBoundaryDeathIndex;
                }

                const WorldPlayerScore* const score = WorldPlayerScoreLookup::FindByEntityKey(playerScores, entityKey);
                if (score == nullptr || score->playerId == 0 || score->lifecycle != WorldPlayerLifecycle::Alive ||
                    causes == WorldPlayerDeathCauseFlags::None)
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }

                if (causes == WorldPlayerDeathCauseFlags::Collision)
                {
                    WorldEntityComponents components;
                    if (!postMovementReadView.TryReadComponents(entityKey, &components) ||
                        components.replicationMetadata.entityKind != WorldEntityKind::Player ||
                        components.playerControl.playerId != score->playerId)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }

                    WorldResult<WorldResourceSpawnPlan> planningResult = WorldResourceSpawnPlanner::PlanDeathDrop(
                        *activeArea, config.resourceCircleRadius, entityKey, components.transform.positionX,
                        components.transform.positionY, components.bodyTrail, score->score, resourceRegistry,
                        reservedResourceSpawnPositions);
                    if (planningResult.Failed() && planningResult.Error() == WorldErrorCode::AllocationFailed)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::AllocationFailed);
                    }
                    if (planningResult.Failed())
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    WorldResourceSpawnPlan spawnPlan = planningResult.TakeValue();
                    if (spawnPlan.rejectedCount >
                        std::numeric_limits<std::uint32_t>::max() - deathDropPlacementFailureCount)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }

                    deathDropPlacementFailureCount += static_cast<std::uint32_t>(spawnPlan.rejectedCount);
                    for (const WorldResourceSpawnRequest& spawn : spawnPlan.requests)
                    {
                        if (spawn.origin != WorldResourceOrigin::DeathDrop || spawn.ambientSlotId != 0 ||
                            spawn.sourceEntityKey != entityKey)
                        {
                            return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                        }
                        deathDropSpawns.push_back(spawn);
                        reservedResourceSpawnPositions.push_back(
                            WorldResourcePosition{spawn.positionX, spawn.positionY});
                    }
                }

                const std::vector<WorldPlayerScore>::iterator projectedScore = std::lower_bound(
                    projectedScores.begin(), projectedScores.end(), score->playerId,
                    [](const WorldPlayerScore& candidate, const std::uint32_t expectedPlayerId) noexcept
                    { return candidate.playerId < expectedPlayerId; });
                if (projectedScore == projectedScores.end() || projectedScore->playerId != score->playerId)
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }

                projectedScore->score = 0;
                projectedScore->boostCostAccumulator = 0.0f;
                projectedScore->lifecycle = WorldPlayerLifecycle::SpawnPending;
                playerDeaths.push_back(WorldPlayerDeathCommit{score->playerId, entityKey, causes});
            }

            std::vector<WorldDisconnectDropSnapshot> orderedDisconnectDropSnapshots(disconnectDropSnapshots.begin(),
                                                                                    disconnectDropSnapshots.end());
            std::sort(orderedDisconnectDropSnapshots.begin(), orderedDisconnectDropSnapshots.end(),
                      [](const WorldDisconnectDropSnapshot& left, const WorldDisconnectDropSnapshot& right) noexcept
                      { return left.sourceEntityKey < right.sourceEntityKey; });
            std::vector<WorldDisconnectDropCommit> disconnectDrops;
            disconnectDrops.reserve(orderedDisconnectDropSnapshots.size());
            for (std::size_t index = 0; index < orderedDisconnectDropSnapshots.size(); ++index)
            {
                const WorldDisconnectDropSnapshot& snapshot = orderedDisconnectDropSnapshots[index];
                if (snapshot.playerId == 0 || !snapshot.sourceEntityKey.IsValid() ||
                    (index > 0 &&
                     !(orderedDisconnectDropSnapshots[index - 1].sourceEntityKey < snapshot.sourceEntityKey)) ||
                    WorldPlayerScoreLookup::FindByEntityKey(playerScores, snapshot.sourceEntityKey) != nullptr)
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }

                if (snapshot.growthPoint > 0)
                {
                    WorldResult<WorldResourceSpawnPlan> planningResult = WorldResourceSpawnPlanner::PlanDeathDrop(
                        *activeArea, config.resourceCircleRadius, snapshot.sourceEntityKey,
                        snapshot.headTransform.positionX, snapshot.headTransform.positionY, snapshot.bodyTrail,
                        snapshot.growthPoint, resourceRegistry, reservedResourceSpawnPositions);
                    if (planningResult.Failed() && planningResult.Error() == WorldErrorCode::AllocationFailed)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::AllocationFailed);
                    }
                    if (planningResult.Failed())
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    WorldResourceSpawnPlan spawnPlan = planningResult.TakeValue();
                    if (spawnPlan.rejectedCount >
                        std::numeric_limits<std::uint32_t>::max() - deathDropPlacementFailureCount)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }

                    deathDropPlacementFailureCount += static_cast<std::uint32_t>(spawnPlan.rejectedCount);
                    for (const WorldResourceSpawnRequest& spawn : spawnPlan.requests)
                    {
                        if (spawn.origin != WorldResourceOrigin::DeathDrop || spawn.ambientSlotId != 0 ||
                            spawn.sourceEntityKey != snapshot.sourceEntityKey)
                        {
                            return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                        }
                        deathDropSpawns.push_back(spawn);
                        reservedResourceSpawnPositions.push_back(
                            WorldResourcePosition{spawn.positionX, spawn.positionY});
                    }
                }
                disconnectDrops.push_back(
                    WorldDisconnectDropCommit{snapshot.playerId, snapshot.sourceEntityKey, snapshot.growthPoint});
            }

            std::sort(deathDropSpawns.begin(), deathDropSpawns.end(),
                      [](const WorldResourceSpawnRequest& left, const WorldResourceSpawnRequest& right) noexcept
                      {
                          if (left.sourceEntityKey != right.sourceEntityKey)
                          {
                              return left.sourceEntityKey < right.sourceEntityKey;
                          }
                          return left.sourceOrdinal < right.sourceOrdinal;
                      });

            std::vector<WorldPlayerBoostCostCommit> boostCosts;
            if (!WorldBoostCostSolver::IsDisabled(config.boostCost))
            {
                boostCosts.reserve(playerScores.size());
                for (std::size_t index = 0; index < playerScores.size(); ++index)
                {
                    const WorldPlayerScore& score = playerScores[index];
                    if (score.playerId == 0 || !score.controlledEntityKey.IsValid() ||
                        (index > 0 && playerScores[index - 1].playerId >= score.playerId) ||
                        (score.lifecycle != WorldPlayerLifecycle::Alive &&
                         score.lifecycle != WorldPlayerLifecycle::SpawnPending))
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    if (projectedScores[index].lifecycle == WorldPlayerLifecycle::SpawnPending)
                    {
                        continue;
                    }

                    WorldEntityComponents components;
                    if (!postMovementReadView.TryReadComponents(score.controlledEntityKey, &components) ||
                        components.playerControl.playerId != score.playerId)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }

                    const WorldBoostCostState currentState{score.score, score.boostCostAccumulator};
                    const bool boostRequested = components.playerControl.boostState == WorldBoostState::On;
                    WorldResult<WorldBoostCostState> nextStateResult =
                        WorldBoostCostSolver::Solve(config.boostCost, currentState, boostRequested, fixedDeltaSeconds);
                    if (nextStateResult.Failed())
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    const WorldBoostCostState nextState = nextStateResult.TakeValue();
                    projectedScores[index].score = nextState.growthPoint;
                    projectedScores[index].boostCostAccumulator = nextState.boostCostAccumulator;
                    if (nextState != currentState)
                    {
                        boostCosts.push_back(
                            WorldPlayerBoostCostCommit{score.playerId, score.controlledEntityKey, nextState});
                    }
                }
            }

            // TriggerOverlap 으로 플레이어와 리소스 충돌 감지
            std::vector<WorldResourcePickup> candidates;
            candidates.reserve(physicsResult.TriggerOverlaps().size());
            for (const WorldTriggerOverlap& overlap : physicsResult.TriggerOverlaps())
            {
                WorldResourcePickup pickup;
                bool isPickup = false;
                const WorldResult<void> classificationResult =
                    ClassifyPickup(overlap, postMovementReadView, projectedScores, resourceSlots, resourceRegistry,
                                   config.resourceScoreValue, &pickup, &isPickup);
                if (classificationResult.Failed())
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(classificationResult.Error());
                }
                if (isPickup)
                {
                    candidates.push_back(pickup);
                }
            }

            std::sort(candidates.begin(), candidates.end(), PickupLess);
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end()); // 중복 후보 제거

            std::vector<WorldResourcePickup> pickups;
            pickups.reserve(candidates.size());
            for (const WorldResourcePickup& candidate : candidates)
            {
                if (pickups.empty() || pickups.back().resourceEntityKey != candidate.resourceEntityKey)
                {
                    pickups.push_back(candidate);
                }
            }

            // Score 계산
            std::vector<WorldPlayerScoreAward> scoreAwards;
            scoreAwards.reserve(pickups.size());
            for (const WorldResourcePickup& pickup : pickups)
            {
                const std::vector<WorldPlayerScoreAward>::iterator found = std::lower_bound(
                    scoreAwards.begin(), // 검색 시작
                    scoreAwards.end(),   // 검색 끝
                    pickup.playerId,     // 찾을 값
                    // comparator(*middle = begin~end 사이 중간값, value = 찾을 값)
                    [](const WorldPlayerScoreAward& award, const std::uint32_t expectedPlayerId) noexcept
                    { return award.playerId < expectedPlayerId; });

                if (found == scoreAwards.end() || found->playerId != pickup.playerId)
                {
                    static_cast<void>(scoreAwards.insert(
                        found, WorldPlayerScoreAward{pickup.playerId, pickup.playerEntityKey, pickup.scoreAward}));
                    continue;
                }

                if (found->controlledEntityKey != pickup.playerEntityKey)
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }
                if (found->scoreAward > std::numeric_limits<std::uint32_t>::max() - pickup.scoreAward)
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::ArithmeticOverflow);
                }
                found->scoreAward += pickup.scoreAward;
            }

            std::vector<WorldResourceInstance> outsideResourceRemovals;
            outsideResourceRemovals.reserve(resourceRegistry.Count());
            std::size_t pickupIndex = 0;
            for (const WorldResourceInstance& resource : resourceRegistry.Instances())
            {
                // pickup 처리된 리소스는 outside removal 대상이 아님
                while (pickupIndex < pickups.size() && pickups[pickupIndex].resourceEntityKey < resource.entityKey)
                {
                    ++pickupIndex;
                }
                if (pickupIndex < pickups.size() && pickups[pickupIndex].resourceEntityKey == resource.entityKey)
                {
                    continue;
                }
                if (!activeArea->ContainsCircleStrictly(resource.positionX, resource.positionY,
                                                        config.resourceCircleRadius))
                {
                    outsideResourceRemovals.push_back(resource);
                }
            }

            WorldGameplayRoundTransition roundTransition;
            std::vector<WorldResourceSpawnRequest> resourceSpawns;
            if (serverTick >= roundState.phaseEndsAtServerTick)
            {
                roundTransition = WorldGameplayRoundTransition{
                    WorldGameplayRoundTransitionKind::Ended,
                    WorldRoundRuntimeState{roundState.roundId, WorldRoundPhase::Ended, serverTick, 0},
                };
            }
            else
            {
                WorldResourceRegistry projectedResourceRegistry = resourceRegistry;
                for (const WorldResourcePickup& pickup : pickups)
                {
                    if (projectedResourceRegistry.TryRemove(pickup.resourceEntityKey, pickup.resourceEntityHandle) !=
                        WorldResourceRemoveResult::Removed)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                }
                for (const WorldResourceInstance& resource : outsideResourceRemovals)
                {
                    if (projectedResourceRegistry.TryRemove(resource.entityKey, resource.entityHandle) !=
                        WorldResourceRemoveResult::Removed)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                }
                if (deathDropSpawns.size() >
                    std::numeric_limits<std::size_t>::max() - projectedResourceRegistry.Count())
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::ArithmeticOverflow);
                }
                const std::size_t projectedResourceCount = projectedResourceRegistry.Count() + deathDropSpawns.size();
                WorldResult<WorldResourcePopulationPlan> populationResult = WorldResourcePopulationSolver::Solve(
                    *activeArea, config.resourceDensityPerUnit2, projectedResourceCount);
                if (populationResult.Failed() && populationResult.Error() == WorldErrorCode::ArithmeticOverflow)
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::ArithmeticOverflow);
                }
                if (populationResult.Failed())
                {
                    return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                }
                const WorldResourcePopulationPlan populationPlan = populationResult.TakeValue();

                resourceSpawns.reserve(populationPlan.shortage);
                std::uint32_t remainingShortage = populationPlan.shortage;
                for (std::size_t index = 0; index < resourceSlots.size(); ++index)
                {
                    const WorldResourceSlotState& slot = resourceSlots[index];
                    if (slot.slotId == 0 || (index > 0 && resourceSlots[index - 1].slotId >= slot.slotId) ||
                        (slot.phase != WorldResourceSlotPhase::Active &&
                         slot.phase != WorldResourceSlotPhase::Respawning))
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    const bool replacesPickup = WorldResourcePickup::ContainsAmbientPickupForSlot(pickups, slot.slotId);
                    const bool replacesOutsideResource =
                        WorldResourceInstance::ContainsAmbientSlot(outsideResourceRemovals, slot.slotId);
                    if (remainingShortage == 0 ||
                        (slot.phase == WorldResourceSlotPhase::Active && !replacesPickup && !replacesOutsideResource))
                    {
                        continue;
                    }

                    WorldResult<WorldResourceSpawnPlan> planningResult = WorldResourceSpawnPlanner::PlanAmbient(
                        *activeArea, config.resourceCircleRadius, serverTick, slot.slotId, projectedResourceRegistry,
                        reservedResourceSpawnPositions);
                    if (planningResult.Failed() && planningResult.Error() == WorldErrorCode::CapacityExceeded)
                    {
                        continue;
                    }
                    if (planningResult.Failed() && planningResult.Error() == WorldErrorCode::AllocationFailed)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::AllocationFailed);
                    }
                    if (planningResult.Failed())
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    WorldResourceSpawnPlan spawnPlan = planningResult.TakeValue();
                    if (spawnPlan.requests.size() != 1 || spawnPlan.rejectedCount != 0)
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    const WorldResourceSpawnRequest& spawn = spawnPlan.requests[0];
                    if (spawn.origin != WorldResourceOrigin::Ambient || spawn.ambientSlotId != slot.slotId ||
                        spawn.sourceEntityKey.IsValid())
                    {
                        return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::InvalidState);
                    }
                    resourceSpawns.push_back(spawn);
                    reservedResourceSpawnPositions.push_back(WorldResourcePosition{spawn.positionX, spawn.positionY});
                    --remainingShortage;
                }
            }

            return WorldResult<WorldGameplayPhaseResult>(WorldGameplayPhaseResult{
                serverTick, std::move(pickups), std::move(scoreAwards), std::move(resourceSpawns), roundTransition,
                std::move(boostCosts), std::move(playerDeaths), std::move(deathDropSpawns),
                deathDropPlacementFailureCount, std::move(outsideResourceRemovals), std::move(disconnectDrops)});
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldGameplayPhaseResult>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
