#include "pch.h"

#include "WorldTickProcessor.h"

#include "WorldMovementPhase.h"
#include "WorldMovementPhaseCommitter.h"
#include "WorldMovementPhaseResult.h"
#include "WorldPhysicsValues.h"
#include "WorldReadView.h"
#include "WorldTickInput.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

namespace psnr::world
{
    namespace
    {
        constexpr std::uint32_t PlayerPhysicsLayer = 1u << 0;
        constexpr std::uint32_t StaticObstaclePhysicsLayer = 1u << 1;
        constexpr std::uint32_t ResourceTriggerPhysicsLayer = 1u << 2;
        constexpr PhysicsFixtureId PrimaryCircleFixture{1};

        [[nodiscard]] PhysicsProxy MakePrimaryCircleProxy(const WorldEntityKey entityKey,
                                                          const EntityHandle entityHandle,
                                                          const WorldEntityComponents& components,
                                                          const PhysicsProxyBehavior behavior,
                                                          const std::uint32_t layer, const std::uint32_t mask) noexcept
        {
            return PhysicsProxy{
                entityKey, entityHandle, PrimaryCircleFixture,
                0.0f,      0.0f,         PhysicsCircleShape{components.replicationMetadata.primaryCircleRadius},
                layer,     mask,         behavior,
            };
        }

        [[nodiscard]] bool IsControlMovementDisabled(const WorldControlMovementConfig& config) noexcept
        {
            return config.baseSpeed == 0.0f && config.boostSpeed == 0.0f && config.angularSpeedRadiansPerSecond == 0.0f;
        }

        [[nodiscard]] bool IsEntirePlayerBodyInsideActiveArea(const WorldActiveArea& activeArea,
                                                              const WorldEntityComponents& components) noexcept
        {
            const float bodyRadius = components.replicationMetadata.primaryCircleRadius;
            if (!components.bodyTrail.IsInitialized() || components.bodyTrail.Empty() ||
                !activeArea.ContainsCircleStrictly(components.transform.positionX, components.transform.positionY,
                                                   bodyRadius))
            {
                return false;
            }

            // body centerline의 각 점을 body radius만큼 축소한 ActiveArea 안에서 검사한다.
            // 축소된 원은 볼록하므로 모든 점이 안에 있으면 점 사이의 body segment도 전부 안에 있다.
            for (std::size_t index = 0; index < components.bodyTrail.SampleCount(); ++index)
            {
                BodyTrailSample sample;
                if (!components.bodyTrail.TryRead(index, &sample) ||
                    !activeArea.ContainsCircleStrictly(sample.positionX, sample.positionY, bodyRadius))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool TryRecordBodyTrailSamples(const std::uint32_t serverTick,
                                                     const WorldBodyTrailSampleConfig& config,
                                                     const WorldMovementPhaseResult& movementResult,
                                                     WorldEntityManager& entityManager)
        {
            if (config.sampleIntervalTicks == 0)
            {
                return true;
            }

            for (const WorldMovementEntityUpdate& update : movementResult.Updates())
            {
                EntityHandle handle;
                WorldEntityComponents components;
                if (!entityManager.TryFindHandle(update.entityKey, &handle) ||
                    !entityManager.TryReadComponents(handle, &components) ||
                    components.replicationMetadata.entityKind != WorldEntityKind::Player ||
                    components.transform != update.transform)
                {
                    return false;
                }

                const WorldBodyTrailSampleResult sampleResult =
                    WorldBodyTrailSampler::Sample(config, serverTick, components.transform.positionX,
                                                  components.transform.positionY, components.bodyTrail);
                if (sampleResult == WorldBodyTrailSampleResult::NotDue)
                {
                    continue;
                }
                if (sampleResult != WorldBodyTrailSampleResult::Sampled ||
                    !entityManager.TryReplaceComponents(handle, components))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool TryBuildSimulationSessions(const std::span<const WorldSession> joinedSessions,
                                                      const std::span<const WorldPlayerScore> playerScores,
                                                      std::vector<WorldSession>* const outSessions)
        {
            assert(outSessions != nullptr);

            if (playerScores.empty())
            {
                outSessions->assign(joinedSessions.begin(), joinedSessions.end());
                return true;
            }

            std::vector<WorldSession> simulationSessions;
            simulationSessions.reserve(joinedSessions.size());
            for (const WorldSession& session : joinedSessions)
            {
                const WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindByPlayerId(playerScores, session.playerId);
                if (!session.IsJoined() || score == nullptr || score->controlledEntityKey != session.entityKey ||
                    (score->lifecycle != WorldPlayerLifecycle::Alive &&
                     score->lifecycle != WorldPlayerLifecycle::SpawnPending))
                {
                    return false;
                }
                if (score->lifecycle == WorldPlayerLifecycle::Alive)
                {
                    simulationSessions.push_back(session);
                }
            }

            *outSessions = std::move(simulationSessions);
            return true;
        }

        [[nodiscard]] bool TryBuildControlMovementInputs(const WorldControlMovementConfig& config,
                                                         const float fixedDeltaSeconds,
                                                         const std::span<const WorldSession> joinedSessions,
                                                         const std::span<const WorldPlayerScore> playerScores,
                                                         const WorldEntityManager& entityManager,
                                                         std::vector<WorldMovementTickInput>* const outInputs)
        {
            assert(outInputs != nullptr);

            std::vector<WorldMovementTickInput> inputs;
            inputs.reserve(joinedSessions.size());
            for (const WorldSession& session : joinedSessions)
            {
                EntityHandle entityHandle;
                WorldEntityComponents components;
                const WorldPlayerScore* const score =
                    WorldPlayerScoreLookup::FindByPlayerId(playerScores, session.playerId);
                if (!session.IsJoined() || score == nullptr || score->controlledEntityKey != session.entityKey ||
                    !entityManager.TryFindHandle(session.entityKey, &entityHandle) ||
                    !entityManager.TryReadComponents(entityHandle, &components) ||
                    components.playerControl.playerId != session.playerId)
                {
                    return false;
                }

                const bool boostActive = components.playerControl.boostState == WorldBoostState::On && score->score > 0;
                WorldResult<WorldControlMovementOutput> movementResult =
                    WorldControlMovementSolver::Solve(config,
                                                      WorldControlMovementInput{
                                                          components.transform.angleRadians,
                                                          components.playerControl.turnState,
                                                          boostActive,
                                                      },
                                                      fixedDeltaSeconds);
                if (movementResult.Failed())
                {
                    return false;
                }
                const WorldControlMovementOutput movement = movementResult.TakeValue();

                inputs.push_back(WorldMovementTickInput{
                    session.sessionKey,
                    session.playerId,
                    session.entityKey,
                    movement.directionX,
                    movement.directionY,
                    true,
                    movement.headingRadians,
                    movement.speed,
                });
            }

            *outInputs = std::move(inputs);
            return true;
        }
    } // namespace

    WorldTickProcessor::WorldTickProcessor(const WorldTickProcessorConfig& config) noexcept
        : config_(config)
    {
        if (IsValid(config_.physics) && WorldPhysicsArenaBounds::IsValid(config_.arenaBounds))
        {
            WorldResult<std::unique_ptr<WorldPhysicsScene>> physicsSceneResult =
                WorldPhysicsScene::Create(config_.physics);
            if (physicsSceneResult.Succeeded())
            {
                physicsScene_ = physicsSceneResult.TakeValue();
            }
        }
    }

    WorldTickProcessResult WorldTickProcessor::Process(const WorldInboundMode inboundMode,
                                                       const std::uint32_t serverTick, const float fixedDeltaSeconds,
                                                       const std::span<const WorldSession> joinedSessions,
                                                       WorldMovementCommandStore& commandStore,
                                                       WorldEntityManager& entityManager,
                                                       const std::span<const WorldPlayerScore> playerScores,
                                                       const WorldActiveArea* const activeArea)
    {
        lastCollisionDeathSet_.clear();
        lastActiveAreaBoundaryDeathSet_.clear();
        lastPlayerSpawnCandidates_.clear();
        if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f)
        {
            return WorldTickProcessResult::InvalidFixedDelta;
        }
        if (activeArea != nullptr && !activeArea->IsValid())
        {
            return WorldTickProcessResult::InvalidActiveArea;
        }

        std::vector<WorldMovementTickInput> movementInputs;
        std::vector<WorldSession> simulationSessions;
        if (!TryBuildSimulationSessions(joinedSessions, playerScores, &simulationSessions))
        {
            return WorldTickProcessResult::EntityStateInvariantViolation;
        }
        const std::span<const WorldSession> simulationSessionView = simulationSessions;
        WorldResult<std::vector<WorldMovementTickInput>> inputBuildResult =
            movementInputBuilder_.BuildTickInputs(inboundMode, serverTick, simulationSessionView, commandStore);
        if (inputBuildResult.Failed() && inputBuildResult.Error() == WorldErrorCode::InvalidArgument)
        {
            return WorldTickProcessResult::EntityStateInvariantViolation;
        }
        if (inputBuildResult.Failed() && inputBuildResult.Error() == WorldErrorCode::InvalidSessionSet)
        {
            return WorldTickProcessResult::InvalidSessionSet;
        }
        if (inputBuildResult.Failed() && inputBuildResult.Error() == WorldErrorCode::NonSequentialTick)
        {
            return WorldTickProcessResult::NonSequentialServerTick;
        }
        if (inputBuildResult.Failed())
        {
            return WorldTickProcessResult::EntityStateInvariantViolation;
        }
        movementInputs = inputBuildResult.TakeValue();

        if (!IsControlMovementDisabled(config_.controlMovement))
        {
            if (!WorldControlMovementSolver::IsValidConfig(config_.controlMovement))
            {
                return WorldTickProcessResult::InvalidControlMovementConfig;
            }
            if (!TryBuildControlMovementInputs(config_.controlMovement, fixedDeltaSeconds, simulationSessionView,
                                               playerScores, entityManager, &movementInputs))
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }
        }

        const WorldTickInput tickInput{serverTick, std::move(movementInputs)};
        const WorldReadView readView{entityManager};
        const std::span<const WorldMovementTickInput> immutableMovementInputs = tickInput.MovementInputs();

        if (PhysicsEnabled()) // physics step
        {
            return ProcessWithPhysics(serverTick, immutableMovementInputs, fixedDeltaSeconds, entityManager,
                                      playerScores, activeArea);
        }

        // -----------------

        if (IsValid(config_.physics) || WorldPhysicsArenaBounds::IsValid(config_.arenaBounds))
        {
            return WorldTickProcessResult::PhysicsComputeFailed;
        }

        lastPhysicsResult_ = WorldPhysicsStepResult{};
        std::vector<WorldMovementEntityUpdate> updates(immutableMovementInputs.size());
        const WorldMovementPhaseComputeResult computeResult =
            WorldMovementPhase::Compute(immutableMovementInputs, fixedDeltaSeconds, readView, updates);
        if (computeResult == WorldMovementPhaseComputeResult::InvalidArgument)
        {
            return WorldTickProcessResult::InvalidFixedDelta;
        }
        if (computeResult != WorldMovementPhaseComputeResult::Computed)
        {
            return WorldTickProcessResult::EntityStateInvariantViolation;
        }

        const WorldMovementPhaseResult movementResult{std::move(updates)};
        WorldMovementPhaseCommitter::Commit(movementResult, entityManager);
        if (!TryRecordBodyTrailSamples(serverTick, config_.bodyTrailSample, movementResult, entityManager))
        {
            return WorldTickProcessResult::BodyTrailSampleFailed;
        }
        std::vector<WorldEntityKey> activeAreaBoundaryDeathSet;
        const WorldTickProcessResult activeAreaResult =
            CollectActiveAreaBoundaryDeaths(activeArea, playerScores, entityManager, &activeAreaBoundaryDeathSet);
        if (activeAreaResult != WorldTickProcessResult::Processed)
        {
            return activeAreaResult;
        }
        lastActiveAreaBoundaryDeathSet_ = std::move(activeAreaBoundaryDeathSet);
        return WorldTickProcessResult::Processed;
    }

    WorldTickProcessResult WorldTickProcessor::Process(const std::uint32_t serverTick, const float fixedDeltaSeconds,
                                                       const std::span<const WorldSession> joinedSessions,
                                                       WorldMovementCommandStore& commandStore,
                                                       WorldEntityManager& entityManager,
                                                       const std::span<const WorldPlayerScore> playerScores,
                                                       const WorldActiveArea* const activeArea)
    {
        return Process(WorldInboundMode::TargetServerTick, serverTick, fixedDeltaSeconds, joinedSessions, commandStore,
                       entityManager, playerScores, activeArea);
    }

    bool WorldTickProcessor::PhysicsEnabled() const noexcept
    {
        return physicsScene_ != nullptr;
    }

    const WorldPhysicsStepResult& WorldTickProcessor::LastPhysicsResult() const noexcept
    {
        return lastPhysicsResult_;
    }

    std::span<const WorldEntityKey> WorldTickProcessor::LastCollisionDeathSet() const noexcept
    {
        return lastCollisionDeathSet_;
    }

    std::span<const WorldEntityKey> WorldTickProcessor::LastActiveAreaBoundaryDeathSet() const noexcept
    {
        return lastActiveAreaBoundaryDeathSet_;
    }

    std::span<const WorldPlayerSpawnCandidate> WorldTickProcessor::LastPlayerSpawnCandidates() const noexcept
    {
        return lastPlayerSpawnCandidates_;
    }

    WorldTickProcessResult WorldTickProcessor::ProcessWithPhysics(
        const std::uint32_t serverTick, const std::span<const WorldMovementTickInput> movementInputs,
        const float fixedDeltaSeconds, WorldEntityManager& entityManager,
        const std::span<const WorldPlayerScore> playerScores, const WorldActiveArea* const activeArea)
    {
        const WorldReadView readView{entityManager};
        std::vector<WorldPhysicsMovementInput> physicsInputs;
        physicsInputs.reserve(movementInputs.size());
        for (const WorldMovementTickInput& movementInput : movementInputs)
        {
            EntityHandle entityHandle;
            WorldEntityComponents components;
            if (!entityManager.TryFindHandle(movementInput.entityKey, &entityHandle) ||
                !readView.TryReadComponents(movementInput.entityKey, &components) ||
                components.replicationMetadata.entityKind != WorldEntityKind::Player ||
                components.replicationMetadata.primaryShapeKind != WorldShapeKind::Circle)
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }

            const PhysicsProxy proxy =
                MakePrimaryCircleProxy(movementInput.entityKey, entityHandle, components, PhysicsProxyBehavior::Solid,
                                       PlayerPhysicsLayer, StaticObstaclePhysicsLayer | ResourceTriggerPhysicsLayer);
            physicsInputs.push_back(WorldPhysicsMovementInput{
                WorldPhysicsProxyProjection{
                    proxy,
                    components.transform.positionX,
                    components.transform.positionY,
                },
                movementInput.movementInputX,
                movementInput.movementInputY,
                movementInput.usesControlMovement ? movementInput.moveSpeed
                                                  : components.movementCapability.maxMoveSpeed,
            });
        }

        std::vector<WorldPhysicsProxyProjection> staticSolidProxies;
        std::vector<WorldPhysicsProxyProjection> triggerProxies;
        const std::span<const EntityHandle> activeHandles = entityManager.ActiveHandles();
        staticSolidProxies.reserve(activeHandles.size());
        triggerProxies.reserve(activeHandles.size());

        for (const EntityHandle entityHandle : activeHandles)
        {
            WorldEntityKey entityKey;
            const WorldEntityComponents* components = nullptr;
            if (!entityManager.TryFindKey(entityHandle, &entityKey) ||
                !entityManager.TryReadComponentsView(entityHandle, &components) ||
                components->replicationMetadata.primaryShapeKind != WorldShapeKind::Circle)
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }

            PhysicsProxyBehavior behavior = PhysicsProxyBehavior::Invalid;
            std::uint32_t layer = 0;
            std::uint32_t mask = 0;
            if (components->replicationMetadata.entityKind == WorldEntityKind::StaticObstacle)
            {
                behavior = PhysicsProxyBehavior::Solid;
                layer = StaticObstaclePhysicsLayer;
                mask = PlayerPhysicsLayer;
            }
            else if (components->replicationMetadata.entityKind == WorldEntityKind::Resource)
            {
                behavior = PhysicsProxyBehavior::Trigger;
                layer = ResourceTriggerPhysicsLayer;
                mask = PlayerPhysicsLayer;
            }
            else if (components->replicationMetadata.entityKind == WorldEntityKind::Player)
            {
                continue;
            }
            else
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }

            const PhysicsProxy proxy =
                MakePrimaryCircleProxy(entityKey, entityHandle, *components, behavior, layer, mask);
            const WorldPhysicsProxyProjection projection{
                proxy,
                components->transform.positionX,
                components->transform.positionY,
            };
            if (behavior == PhysicsProxyBehavior::Solid)
            {
                staticSolidProxies.push_back(projection);
            }
            else
            {
                triggerProxies.push_back(projection);
            }
        }

        WorldResult<WorldPhysicsStepResult> computeResult = physicsScene_->Compute(
            physicsInputs, staticSolidProxies, triggerProxies, config_.arenaBounds, fixedDeltaSeconds);
        if (computeResult.Failed() && computeResult.Error() == WorldErrorCode::InitialPenetration)
        {
            return WorldTickProcessResult::PhysicsInitialPenetration;
        }
        if (computeResult.Failed())
        {
            return WorldTickProcessResult::PhysicsComputeFailed;
        }
        WorldPhysicsStepResult physicsResult = computeResult.TakeValue();
        if (physicsResult.ResolvedMotions().size() != movementInputs.size())
        {
            return WorldTickProcessResult::PhysicsComputeFailed;
        }

        // physics 결과 반영
        std::vector<WorldMovementEntityUpdate> updates;
        updates.reserve(movementInputs.size());
        const std::span<const WorldResolvedMotion> resolvedMotions = physicsResult.ResolvedMotions();
        for (std::size_t index = 0; index < resolvedMotions.size(); ++index)
        {
            const WorldResolvedMotion& resolvedMotion = resolvedMotions[index];
            const WorldMovementTickInput& movementInput = movementInputs[index];
            WorldEntityComponents components;
            EntityHandle currentHandle;
            if (resolvedMotion.ownerKey != movementInput.entityKey ||
                !entityManager.TryFindHandle(resolvedMotion.ownerKey, &currentHandle) ||
                currentHandle != resolvedMotion.ownerHandle ||
                !readView.TryReadComponents(resolvedMotion.ownerKey, &components))
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }

            components.transform.positionX = resolvedMotion.positionX;
            components.transform.positionY = resolvedMotion.positionY;
            if (movementInput.usesControlMovement)
            {
                components.transform.angleRadians = movementInput.headingRadians;
            }
            components.motion.velocityX = resolvedMotion.velocityX;
            components.motion.velocityY = resolvedMotion.velocityY;
            components.motion.movementIntentX = movementInput.movementInputX;
            components.motion.movementIntentY = movementInput.movementInputY;
            updates.push_back(WorldMovementEntityUpdate{
                resolvedMotion.ownerKey,
                components.transform,
                components.motion,
            });
        }

        // 입력 처리
        const WorldMovementPhaseResult movementResult{std::move(updates)};
        WorldMovementPhaseCommitter::Commit(movementResult, entityManager);
        if (!TryRecordBodyTrailSamples(serverTick, config_.bodyTrailSample, movementResult, entityManager))
        {
            return WorldTickProcessResult::BodyTrailSampleFailed;
        }

        std::vector<WorldEntityKey> activeAreaBoundaryDeathSet;
        const WorldTickProcessResult activeAreaResult =
            CollectActiveAreaBoundaryDeaths(activeArea, playerScores, entityManager, &activeAreaBoundaryDeathSet);
        if (activeAreaResult != WorldTickProcessResult::Processed)
        {
            return activeAreaResult;
        }

        const WorldTickProcessResult collisionResult = ProcessPlayerCollisions(playerScores, entityManager);
        if (collisionResult != WorldTickProcessResult::Processed)
        {
            return collisionResult;
        }
        const WorldTickProcessResult spawnPlanResult = PlanPlayerSpawns(serverTick, playerScores, activeArea);
        if (spawnPlanResult != WorldTickProcessResult::Processed)
        {
            return spawnPlanResult;
        }
        lastActiveAreaBoundaryDeathSet_ = std::move(activeAreaBoundaryDeathSet);
        lastPhysicsResult_ = std::move(physicsResult);
        return WorldTickProcessResult::Processed;
    }

    WorldTickProcessResult WorldTickProcessor::CollectActiveAreaBoundaryDeaths(
        const WorldActiveArea* const activeArea, const std::span<const WorldPlayerScore> playerScores,
        const WorldEntityManager& entityManager, std::vector<WorldEntityKey>* const outDeathSet) const
    {
        assert(outDeathSet != nullptr);

        std::vector<WorldEntityKey> deathSet;
        if (activeArea == nullptr)
        {
            *outDeathSet = std::move(deathSet);
            return WorldTickProcessResult::Processed;
        }

        try
        {
            deathSet.reserve(playerScores.size());
            for (const WorldPlayerScore& score : playerScores)
            {
                if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
                {
                    continue;
                }
                if (score.playerId == 0 || score.lifecycle != WorldPlayerLifecycle::Alive ||
                    !score.controlledEntityKey.IsValid())
                {
                    return WorldTickProcessResult::EntityStateInvariantViolation;
                }

                EntityHandle entityHandle;
                WorldEntityComponents components;
                if (!entityManager.TryFindHandle(score.controlledEntityKey, &entityHandle) ||
                    !entityManager.TryReadComponents(entityHandle, &components) ||
                    components.replicationMetadata.entityKind != WorldEntityKind::Player ||
                    components.replicationMetadata.primaryShapeKind != WorldShapeKind::Circle ||
                    components.playerControl.playerId != score.playerId ||
                    !std::isfinite(components.replicationMetadata.primaryCircleRadius) ||
                    components.replicationMetadata.primaryCircleRadius <= 0.0f)
                {
                    return WorldTickProcessResult::EntityStateInvariantViolation;
                }

                if (!activeArea->ContainsCircleStrictly(components.transform.positionX, components.transform.positionY,
                                                        components.replicationMetadata.primaryCircleRadius))
                {
                    deathSet.push_back(score.controlledEntityKey);
                }
            }
            std::sort(deathSet.begin(), deathSet.end());
            if (std::adjacent_find(deathSet.begin(), deathSet.end()) != deathSet.end())
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }
        }
        catch (...)
        {
            return WorldTickProcessResult::ActiveAreaDeathCollectFailed;
        }

        *outDeathSet = std::move(deathSet);
        return WorldTickProcessResult::Processed;
    }

    WorldTickProcessResult WorldTickProcessor::ProcessPlayerCollisions(
        const std::span<const WorldPlayerScore> playerScores, WorldEntityManager& entityManager)
    {
        if (!WorldPlayerBody::IsEnabled(config_.playerBody))
        {
            return WorldTickProcessResult::Processed;
        }
        if (!WorldPlayerBody::IsValidConfig(config_.playerBody))
        {
            return WorldTickProcessResult::BodyFinalizeFailed;
        }

        collisionProxyBatch_.Clear();
        for (const WorldPlayerScore& score : playerScores)
        {
            if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
            {
                continue;
            }
            if (score.lifecycle != WorldPlayerLifecycle::Alive)
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }

            EntityHandle entityHandle;
            WorldEntityComponents components;
            if (score.playerId == 0 || !score.controlledEntityKey.IsValid() ||
                !entityManager.TryFindHandle(score.controlledEntityKey, &entityHandle) ||
                !entityManager.TryReadComponents(entityHandle, &components) ||
                components.playerControl.playerId != score.playerId)
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }

            // growthPoint 계산, Trim, capsule rad 계산
            if (WorldPlayerBody::Finalize(config_.playerBody, score.score, &components) !=
                WorldPlayerBodyUpdateResult::Updated)
            {
                return WorldTickProcessResult::BodyFinalizeFailed;
            }

            // collision 대상으로 추가
            if (collisionProxyBatch_.AppendPlayer(score.controlledEntityKey, components) !=
                WorldCollisionProxyAppendResult::Appended)
            {
                return WorldTickProcessResult::CollisionProjectionFailed;
            }
        }

        WorldResult<std::vector<WorldCollisionContact>> contactsResult =
            physicsScene_->QueryPlayerCollisions(collisionProxyBatch_.Proxies());
        if (contactsResult.Failed())
        {
            return WorldTickProcessResult::CollisionQueryFailed;
        }
        const std::vector<WorldCollisionContact> contacts = contactsResult.TakeValue();

        WorldResult<std::vector<WorldEntityKey>> deathSetResult = WorldCollisionDeathResolver::Resolve(contacts);
        if (deathSetResult.Failed())
        {
            return WorldTickProcessResult::CollisionDeathResolveFailed;
        }

        lastCollisionDeathSet_ = deathSetResult.TakeValue();
        return WorldTickProcessResult::Processed;
    }

    WorldTickProcessResult WorldTickProcessor::PlanPlayerSpawns(const std::uint32_t serverTick,
                                                                const std::span<const WorldPlayerScore> playerScores,
                                                                const WorldActiveArea* const activeArea)
    {
        std::size_t pendingPlayerCount = 0;
        for (const WorldPlayerScore& score : playerScores)
        {
            if (score.playerId == 0 || !score.controlledEntityKey.IsValid() ||
                (score.lifecycle != WorldPlayerLifecycle::Alive &&
                 score.lifecycle != WorldPlayerLifecycle::SpawnPending))
            {
                return WorldTickProcessResult::EntityStateInvariantViolation;
            }
            if (score.lifecycle == WorldPlayerLifecycle::SpawnPending)
            {
                ++pendingPlayerCount;
            }
        }
        if (pendingPlayerCount == 0)
        {
            return WorldTickProcessResult::Processed;
        }

        const WorldPlayerSpawnPlannerConfig spawnConfig{
            config_.arenaBounds,       config_.playerSpawnMaxCandidatesPerTick,
            config_.playerArchetypeId, config_.controlMovement.baseSpeed,
            config_.playerBody,
        };
        if (spawnConfig.maxCandidatesPerTick == 0)
        {
            return WorldTickProcessResult::PlayerSpawnPlanFailed;
        }
        std::vector<WorldPlayerSpawnCandidate> plannedCandidates;
        try
        {
            plannedCandidates.reserve(pendingPlayerCount);
        }
        catch (...)
        {
            return WorldTickProcessResult::PlayerSpawnPlanFailed;
        }

        for (const WorldPlayerScore& score : playerScores)
        {
            if (score.lifecycle != WorldPlayerLifecycle::SpawnPending)
            {
                continue;
            }

            for (std::uint32_t ordinal = 0; ordinal < spawnConfig.maxCandidatesPerTick; ++ordinal)
            {
                WorldResult<WorldPlayerSpawnCandidate> candidateResult =
                    WorldPlayerSpawnPlanner::Plan(spawnConfig, serverTick, score.playerId, ordinal);
                if (candidateResult.Failed())
                {
                    return WorldTickProcessResult::PlayerSpawnPlanFailed;
                }
                WorldPlayerSpawnCandidate candidate = candidateResult.TakeValue();
                if (activeArea != nullptr && !IsEntirePlayerBodyInsideActiveArea(*activeArea, candidate.components))
                {
                    continue;
                }

                const WorldPlayerSpawnReservationResult reservationResult =
                    physicsScene_->TryReservePlayerSpawnPlacement(candidate.bounds);
                if (reservationResult == WorldPlayerSpawnReservationResult::Blocked)
                {
                    continue;
                }
                if (reservationResult != WorldPlayerSpawnReservationResult::Reserved)
                {
                    return WorldTickProcessResult::PlayerSpawnReservationFailed;
                }

                plannedCandidates.push_back(std::move(candidate));
                break;
            }
        }

        lastPlayerSpawnCandidates_ = std::move(plannedCandidates);
        return WorldTickProcessResult::Processed;
    }
} // namespace psnr::world
