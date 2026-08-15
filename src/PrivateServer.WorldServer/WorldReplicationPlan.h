#pragma once

#include "EntityRemove.h"
#include "EntitySpawn.h"
#include "EntityStateBatch.h"
#include "WorldAoiPlanner.h"
#include "WorldEntityManager.h"
#include "WorldSessionRegistry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    struct WorldReplicationRecipientPlan final
    {
        WorldSessionKey sessionKey{};
        std::vector<protocol::v1::EntityRemove> removes;
        std::vector<protocol::v2::EntitySpawn> spawns;
        std::vector<protocol::v1::EntityStateRecord> stateRecords;
    };

    struct WorldReplicationPlan final
    {
        std::uint32_t serverTick = 0;
        std::vector<WorldReplicationRecipientPlan> recipients;
    };

    class WorldReplicationPlanner final
    {
    public:
        [[nodiscard]] WorldResult<WorldReplicationPlan> Build(std::uint32_t serverTick,
                                                              std::span<const WorldAoiVisibilityDiff> visibilityDiffs,
                                                              const WorldEntityManager& entityManager,
                                                              std::span<const WorldSession> joinedSessions,
                                                              bool includeStateRecords) const noexcept;
    };
} // namespace psnr::world
