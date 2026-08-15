#pragma once

#include "EntitySpawn.h"
#include "WorldEntityManager.h"
#include "WorldPlayerBody.h"
#include "WorldReady.h"
#include "WorldSessionRegistry.h"

#include <cstdint>
#include <span>

namespace psnr::world
{
    struct WorldJoinConfig final
    {
        std::uint32_t tickRateHz = 0;
        std::uint32_t snapshotIntervalTicks = 0;
        std::uint32_t commandSlackTicks = 0;
        float arenaMinX = 0.0f;
        float arenaMinY = 0.0f;
        float arenaMaxX = 0.0f;
        float arenaMaxY = 0.0f;

        std::uint32_t playerArchetypeId = 0;
        float playerCircleRadius = 0.0f;
        float playerMaxMoveSpeed = 0.0f;
        float playerSpawnX = 0.0f;
        float playerSpawnY = 0.0f;
        WorldPlayerBodyConfig playerBody{};
        std::uint32_t channelId = 0;
    };

    struct WorldJoinBaseline final
    {
        WorldEntityKey entityKey{};
        EntityHandle entityHandle{};
        protocol::v2::EntitySpawn entitySpawn{};
        protocol::v2::WorldReady worldReady{};
    };

    class WorldJoinIngress final
    {
    public:
        [[nodiscard]] static WorldResult<WorldJoinBaseline> Prepare(const WorldSessionRegistry& sessionRegistry,
                                                                    WorldEntityManager& entityManager,
                                                                    WorldSessionKey sessionKey, std::uint32_t playerId,
                                                                    std::uint32_t currentServerTick,
                                                                    const WorldJoinConfig& config,
                                                                    std::span<const std::byte> payload);
        [[nodiscard]] static bool Commit(WorldSessionRegistry& sessionRegistry, const WorldEntityManager& entityManager,
                                         WorldSessionKey sessionKey, std::uint32_t playerId,
                                         const WorldJoinBaseline& baseline);
        [[nodiscard]] static bool Rollback(WorldEntityManager& entityManager, const WorldJoinBaseline& baseline);

    private:
        [[nodiscard]] static bool IsValid(const WorldJoinConfig& config) noexcept;
    };
} // namespace psnr::world
