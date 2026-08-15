#include "pch.h"

#include "WorldReplicationPlan.h"

#include <algorithm>
#include <new>

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] bool KeySpanIsValid(const std::span<const WorldEntityKey> keys) noexcept
        {
            for (std::size_t index = 0; index < keys.size(); ++index)
            {
                if (!keys[index].IsValid())
                {
                    return false;
                }
            }

            return true;
        }

        struct ReplicationSnapshot final
        {
            WorldEntityKey key{};
            WorldEntityKind entityKind = WorldEntityKind::Invalid;
            protocol::v2::EntitySpawn spawn;
            protocol::v1::EntityStateRecord stateRecord;
        };

        [[nodiscard]] protocol::v1::EntitySpawn MakeSpawnBaseline(const std::uint32_t serverTick,
                                                                  const WorldEntityKey key,
                                                                  const WorldEntityComponents& components) noexcept
        {
            protocol::v1::EntitySpawn spawn;
            spawn.serverTick = serverTick;
            spawn.entityId = key.entityId;
            spawn.generation = key.generation;
            spawn.entityKind = static_cast<protocol::EntityKind>(components.replicationMetadata.entityKind);
            spawn.archetypeId = components.replicationMetadata.archetypeId;
            spawn.primaryShapeKind = static_cast<protocol::ShapeKind>(components.replicationMetadata.primaryShapeKind);
            spawn.primaryCircleRadius = components.replicationMetadata.primaryCircleRadius;
            spawn.maxMoveSpeed = components.movementCapability.maxMoveSpeed;
            spawn.positionX = components.transform.positionX;
            spawn.positionY = components.transform.positionY;
            spawn.velocityX = components.motion.velocityX;
            spawn.velocityY = components.motion.velocityY;
            spawn.angleRadians = components.transform.angleRadians;
            return spawn;
        }

        [[nodiscard]] protocol::v1::EntityStateRecord MakeStateRecord(const WorldEntityKey key,
                                                                      const WorldEntityComponents& components) noexcept
        {
            protocol::v1::EntityStateRecord record;
            record.entityId = key.entityId;
            record.generation = key.generation;
            record.positionX = components.transform.positionX;
            record.positionY = components.transform.positionY;
            record.velocityX = components.motion.velocityX;
            record.velocityY = components.motion.velocityY;
            record.angleRadians = components.transform.angleRadians;
            return record;
        }

        [[nodiscard]] bool RemoveLess(const protocol::v1::EntityRemove& left,
                                      const protocol::v1::EntityRemove& right) noexcept
        {
            return WorldEntityKey{left.entityId, left.generation} < WorldEntityKey{right.entityId, right.generation};
        }

        [[nodiscard]] bool SpawnLess(const protocol::v2::EntitySpawn& left,
                                     const protocol::v2::EntitySpawn& right) noexcept
        {
            return WorldEntityKey{left.baseline.entityId, left.baseline.generation} <
                   WorldEntityKey{right.baseline.entityId, right.baseline.generation};
        }

        [[nodiscard]] bool StateLess(const protocol::v1::EntityStateRecord& left,
                                     const protocol::v1::EntityStateRecord& right) noexcept
        {
            return WorldEntityKey{left.entityId, left.generation} < WorldEntityKey{right.entityId, right.generation};
        }

        [[nodiscard]] bool RecipientLess(const WorldReplicationRecipientPlan& left,
                                         const WorldReplicationRecipientPlan& right) noexcept
        {
            return left.sessionKey.value < right.sessionKey.value;
        }

        [[nodiscard]] bool SnapshotLess(const ReplicationSnapshot& left, const ReplicationSnapshot& right) noexcept
        {
            return left.key < right.key;
        }

        [[nodiscard]] const ReplicationSnapshot* FindSnapshot(const std::vector<ReplicationSnapshot>& snapshots,
                                                              const WorldEntityKey key) noexcept
        {
            const std::vector<ReplicationSnapshot>::const_iterator iterator =
                std::lower_bound(snapshots.begin(), snapshots.end(), ReplicationSnapshot{key}, SnapshotLess);
            return iterator != snapshots.end() && iterator->key == key ? &*iterator : nullptr;
        }
    } // namespace

    WorldResult<WorldReplicationPlan> WorldReplicationPlanner::Build(
        const std::uint32_t serverTick, const std::span<const WorldAoiVisibilityDiff> visibilityDiffs,
        const WorldEntityManager& entityManager, const std::span<const WorldSession> joinedSessions,
        const bool includeStateRecords) const noexcept
    {
        try
        {
            WorldReplicationPlan built;
            built.serverTick = serverTick;
            built.recipients.reserve(visibilityDiffs.size());

            std::vector<WorldEntityKey> snapshotKeys;
            for (std::size_t recipientIndex = 0; recipientIndex < visibilityDiffs.size(); ++recipientIndex)
            {
                const WorldAoiVisibilityDiff& visibilityDiff = visibilityDiffs[recipientIndex];
                if (!visibilityDiff.sessionKey.IsValid() || !KeySpanIsValid(visibilityDiff.entered) ||
                    !KeySpanIsValid(visibilityDiff.stayed) || !KeySpanIsValid(visibilityDiff.left))
                {
                    return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                snapshotKeys.insert(snapshotKeys.end(), visibilityDiff.entered.begin(), visibilityDiff.entered.end());
                if (includeStateRecords)
                {
                    snapshotKeys.insert(snapshotKeys.end(), visibilityDiff.stayed.begin(), visibilityDiff.stayed.end());
                }
            }

            std::sort(snapshotKeys.begin(), snapshotKeys.end());
            snapshotKeys.erase(std::unique(snapshotKeys.begin(), snapshotKeys.end()), snapshotKeys.end());
            std::vector<ReplicationSnapshot> snapshots;
            snapshots.reserve(snapshotKeys.size());
            for (const WorldEntityKey key : snapshotKeys)
            {
                EntityHandle handle;
                const WorldEntityComponents* components = nullptr;
                if (!entityManager.TryFindHandle(key, &handle) ||
                    !entityManager.TryReadComponentsView(handle, &components))
                {
                    return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
                protocol::v2::EntitySpawn spawn;
                spawn.baseline = MakeSpawnBaseline(serverTick, key, *components);
                if (components->replicationMetadata.entityKind == WorldEntityKind::Player)
                {
                    const WorldSession* const session =
                        WorldSessionLookup::FindUniqueByPlayerId(joinedSessions, components->playerControl.playerId);
                    if (session == nullptr)
                    {
                        return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                    }
                    spawn.playerId = session->playerId;
                    spawn.displayName = session->displayName;
                }
                ReplicationSnapshot snapshot{
                    key,
                    components->replicationMetadata.entityKind,
                    std::move(spawn),
                };
                if (includeStateRecords)
                {
                    snapshot.stateRecord = MakeStateRecord(key, *components);
                }
                snapshots.push_back(std::move(snapshot));
            }

            for (std::size_t recipientIndex = 0; recipientIndex < visibilityDiffs.size(); ++recipientIndex)
            {
                const WorldAoiVisibilityDiff& visibilityDiff = visibilityDiffs[recipientIndex];
                WorldReplicationRecipientPlan recipient;
                recipient.sessionKey = visibilityDiff.sessionKey;
                recipient.removes.reserve(visibilityDiff.left.size());
                recipient.spawns.reserve(visibilityDiff.entered.size());
                if (includeStateRecords)
                {
                    recipient.stateRecords.reserve(visibilityDiff.stayed.size());
                }

                for (std::size_t keyIndex = 0; keyIndex < visibilityDiff.left.size(); ++keyIndex)
                {
                    const WorldEntityKey key = visibilityDiff.left[keyIndex];
                    recipient.removes.push_back(protocol::v1::EntityRemove{serverTick, key.entityId, key.generation,
                                                                           protocol::EntityRemoveReason::LeftAoi});
                }

                for (std::size_t keyIndex = 0; keyIndex < visibilityDiff.entered.size(); ++keyIndex)
                {
                    const WorldEntityKey key = visibilityDiff.entered[keyIndex];
                    const ReplicationSnapshot* snapshot = FindSnapshot(snapshots, key);
                    if (snapshot == nullptr)
                    {
                        return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                    }
                    recipient.spawns.push_back(snapshot->spawn);
                }

                if (includeStateRecords)
                {
                    for (std::size_t keyIndex = 0; keyIndex < visibilityDiff.stayed.size(); ++keyIndex)
                    {
                        const WorldEntityKey key = visibilityDiff.stayed[keyIndex];
                        const ReplicationSnapshot* snapshot = FindSnapshot(snapshots, key);
                        if (snapshot == nullptr)
                        {
                            return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                        }
                        if (snapshot->entityKind != WorldEntityKind::StaticObstacle)
                        {
                            recipient.stateRecords.push_back(snapshot->stateRecord);
                        }
                    }
                }

                std::sort(recipient.removes.begin(), recipient.removes.end(), RemoveLess);
                std::sort(recipient.spawns.begin(), recipient.spawns.end(), SpawnLess);
                if (includeStateRecords)
                {
                    std::sort(recipient.stateRecords.begin(), recipient.stateRecords.end(), StateLess);
                }
                built.recipients.push_back(std::move(recipient));
            }

            std::sort(built.recipients.begin(), built.recipients.end(), RecipientLess);
            for (std::size_t index = 1; index < built.recipients.size(); ++index)
            {
                if (built.recipients[index - 1].sessionKey == built.recipients[index].sessionKey)
                {
                    return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::InvalidInput);
                }
            }

            return WorldResult<WorldReplicationPlan>(std::move(built));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<WorldReplicationPlan>::Failure(WorldErrorCode::AllocationFailed);
        }
    }
} // namespace psnr::world
