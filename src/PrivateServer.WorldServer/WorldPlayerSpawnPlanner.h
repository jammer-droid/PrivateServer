#pragma once

#include "WorldCollisionProxyBatch.h"
#include "WorldEntityComponents.h"
#include "WorldPhysicsValues.h"
#include "WorldPlayerBody.h"

#include <cstdint>

namespace psnr::world
{
    struct WorldPlayerSpawnPlannerConfig final
    {
        WorldPhysicsArenaBounds arenaBounds{};
        std::uint32_t maxCandidatesPerTick = 0;
        std::uint32_t playerArchetypeId = 0;
        float playerMaxMoveSpeed = 0.0f;
        WorldPlayerBodyConfig playerBody{};
    };

    struct WorldPlayerSpawnCandidate final
    {
        std::uint32_t playerId = 0;
        std::uint32_t ordinal = 0;
        WorldEntityComponents components{};
        WorldPlayerSpawnBounds bounds{};
    };

    class WorldPlayerSpawnPlanner final
    {
    public:
        // 같은 tick/player/ordinal은 같은 candidate를 만들고, 성공한 candidate만 output에 commit한다.
        [[nodiscard]] static WorldResult<WorldPlayerSpawnCandidate> Plan(const WorldPlayerSpawnPlannerConfig& config,
                                                                         std::uint32_t serverTick,
                                                                         std::uint32_t playerId,
                                                                         std::uint32_t candidateOrdinal) noexcept;

        [[nodiscard]] static bool TryProjectBounds(const WorldEntityComponents& components,
                                                   WorldPlayerSpawnBounds* outBounds) noexcept;
    };
} // namespace psnr::world
