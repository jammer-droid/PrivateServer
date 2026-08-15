#include "pch.h"

#include "WorldJoinIngress.h"

#include "JoinWorldRequest.h"

#include <cmath>
#include <utility>

namespace psnr::world
{
    // World Join 을 위한 Baseline 생성
    WorldResult<WorldJoinBaseline> WorldJoinIngress::Prepare(
        const WorldSessionRegistry& sessionRegistry, WorldEntityManager& entityManager,
        const WorldSessionKey sessionKey, const std::uint32_t playerId, const std::uint32_t currentServerTick,
        const WorldJoinConfig& config, const std::span<const std::byte> payload)
    {
        if (!sessionKey.IsValid() || playerId == 0)
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::InvalidArgument);
        }
        if (!IsValid(config))
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::InvalidConfig);
        }

        protocol::v2::JoinWorldRequest request;
        if (protocol::v2::JoinWorldRequest::Decode(payload, &request) != protocol::WorldProtocolError::Success)
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::MalformedPayload);
        }

        WorldSession session;
        if (!sessionRegistry.TryFind(sessionKey, &session))
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::NotFound);
        }
        if (session.IsJoined())
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::AlreadyExists);
        }

        WorldEntityComponents components;
        components.transform = TransformComponent{config.playerSpawnX, config.playerSpawnY, 0.0f};
        components.motion = MotionComponent{};
        components.movementCapability = MovementCapabilityComponent{config.playerMaxMoveSpeed};
        components.replicationMetadata = ReplicationMetadataComponent{
            WorldEntityKind::Player,
            config.playerArchetypeId,
            WorldShapeKind::Circle,
            config.playerCircleRadius,
        };
        components.playerControl = PlayerControlComponent{playerId};
        const WorldPlayerBodyUpdateResult bodyResult = WorldPlayerBody::IsEnabled(config.playerBody)
                                                           ? WorldPlayerBody::Initialize(config.playerBody, &components)
                                                           : WorldPlayerBodyUpdateResult::Updated;
        if (bodyResult == WorldPlayerBodyUpdateResult::AllocationFailed)
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::AllocationFailed);
        }
        if (bodyResult != WorldPlayerBodyUpdateResult::Updated)
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::DependencyFailure);
        }

        WorldEntityKey entityKey;
        EntityHandle entityHandle;
        if (!entityManager.TryCreate(components, &entityKey, &entityHandle))
        {
            return WorldResult<WorldJoinBaseline>::Failure(WorldErrorCode::OperationFailed);
        }

        WorldJoinBaseline baseline;
        baseline.entityKey = entityKey;
        baseline.entityHandle = entityHandle;
        baseline.entitySpawn = protocol::v2::EntitySpawn{
            protocol::v1::EntitySpawn{
                currentServerTick,
                entityKey.entityId,
                entityKey.generation,
                protocol::EntityKind::Player,
                config.playerArchetypeId,
                protocol::ShapeKind::Circle,
                components.replicationMetadata.primaryCircleRadius,
                config.playerMaxMoveSpeed,
                config.playerSpawnX,
                config.playerSpawnY,
                0.0f,
                0.0f,
                0.0f,
            },
            playerId,
            request.displayName,
        };
        baseline.worldReady = protocol::v2::WorldReady{
            playerId,
            entityKey.entityId,
            entityKey.generation,
            currentServerTick,
            config.tickRateHz,
            config.snapshotIntervalTicks,
            config.commandSlackTicks,
            config.arenaMinX,
            config.arenaMinY,
            config.arenaMaxX,
            config.arenaMaxY,
            config.channelId,
            std::move(request.displayName),
        };

        return WorldResult<WorldJoinBaseline>(std::move(baseline));
    }

    bool WorldJoinIngress::Commit(WorldSessionRegistry& sessionRegistry, const WorldEntityManager& entityManager,
                                  const WorldSessionKey sessionKey, const std::uint32_t playerId,
                                  const WorldJoinBaseline& baseline)
    {
        if (!sessionKey.IsValid() || playerId == 0 || !baseline.entityKey.IsValid() || !baseline.entityHandle.IsValid())
        {
            return false;
        }

        EntityHandle foundHandle;
        if (!entityManager.TryFindHandle(baseline.entityKey, &foundHandle) || foundHandle != baseline.entityHandle)
        {
            return false;
        }

        return sessionRegistry.TryBindPlayer(sessionKey, playerId, baseline.entityKey, baseline.worldReady.displayName);
    }

    bool WorldJoinIngress::Rollback(WorldEntityManager& entityManager, const WorldJoinBaseline& baseline)
    {
        if (!baseline.entityKey.IsValid() || !baseline.entityHandle.IsValid())
        {
            return false;
        }

        EntityHandle foundHandle;
        return entityManager.TryFindHandle(baseline.entityKey, &foundHandle) && foundHandle == baseline.entityHandle &&
               entityManager.Remove(foundHandle);
    }

    bool WorldJoinIngress::IsValid(const WorldJoinConfig& config) noexcept
    {
        return config.tickRateHz > 0 && config.snapshotIntervalTicks > 0 && config.commandSlackTicks > 0 &&
               config.channelId > 0 &&
               std::isfinite(config.arenaMinX) && std::isfinite(config.arenaMinY) && std::isfinite(config.arenaMaxX) &&
               std::isfinite(config.arenaMaxY) && config.arenaMinX < config.arenaMaxX &&
               config.arenaMinY < config.arenaMaxY && config.playerArchetypeId > 0 &&
               std::isfinite(config.playerCircleRadius) && config.playerCircleRadius > 0.0f &&
               std::isfinite(config.playerMaxMoveSpeed) && config.playerMaxMoveSpeed > 0.0f &&
               std::isfinite(config.playerSpawnX) && std::isfinite(config.playerSpawnY) &&
               config.playerSpawnX >= config.arenaMinX && config.playerSpawnX <= config.arenaMaxX &&
               config.playerSpawnY >= config.arenaMinY && config.playerSpawnY <= config.arenaMaxY &&
               (!WorldPlayerBody::IsEnabled(config.playerBody) || WorldPlayerBody::IsValidConfig(config.playerBody));
    }
} // namespace psnr::world
