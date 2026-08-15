#pragma once

#include "WorldActiveAreaSolver.h"
#include "WorldBodyTrailComponent.h"
#include "WorldResourceRegistry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    struct WorldResourceSpawnRequest final
    {
        WorldResourceOrigin origin = WorldResourceOrigin::Invalid;
        std::uint32_t ambientSlotId = 0;
        WorldEntityKey sourceEntityKey{};
        std::uint32_t sourceOrdinal = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;

        WorldResourceSpawnRequest() = default;
        WorldResourceSpawnRequest(WorldResourceOrigin requestOrigin, std::uint32_t requestAmbientSlotId,
                                  WorldEntityKey requestSourceEntityKey, std::uint32_t requestSourceOrdinal,
                                  float requestPositionX, float requestPositionY) noexcept
            : origin(requestOrigin)
            , ambientSlotId(requestAmbientSlotId)
            , sourceEntityKey(requestSourceEntityKey)
            , sourceOrdinal(requestSourceOrdinal)
            , positionX(requestPositionX)
            , positionY(requestPositionY)
        {
        }
        WorldResourceSpawnRequest(std::uint32_t requestAmbientSlotId, float requestPositionX,
                                  float requestPositionY) noexcept
            : WorldResourceSpawnRequest(WorldResourceOrigin::Ambient, requestAmbientSlotId, {}, 0, requestPositionX,
                                        requestPositionY)
        {
        }
        WorldResourceSpawnRequest(WorldEntityKey requestSourceEntityKey, std::uint32_t requestSourceOrdinal,
                                  float requestPositionX, float requestPositionY) noexcept
            : WorldResourceSpawnRequest(WorldResourceOrigin::DeathDrop, 0, requestSourceEntityKey, requestSourceOrdinal,
                                        requestPositionX, requestPositionY)
        {
        }

        [[nodiscard]] friend bool operator==(const WorldResourceSpawnRequest& left,
                                             const WorldResourceSpawnRequest& right) noexcept = default;
    };

    struct WorldResourceSpawnPlan final
    {
        std::vector<WorldResourceSpawnRequest> requests;
        std::size_t rejectedCount = 0;

        [[nodiscard]] friend bool operator==(const WorldResourceSpawnPlan& left,
                                             const WorldResourceSpawnPlan& right) noexcept = default;
    };

    class WorldResourceSpawnPlanner final
    {
    public:
        [[nodiscard]] static WorldResult<WorldResourceSpawnPlan> PlanAmbient(
            const WorldActiveArea& activeArea, float resourceCircleRadius, std::uint32_t serverTick,
            std::uint32_t ambientSlotId, const WorldResourceRegistry& resourceRegistry,
            std::span<const WorldResourcePosition> reservedPositions) noexcept;

        [[nodiscard]] static WorldResult<WorldResourceSpawnPlan> PlanDeathDrop(
            const WorldActiveArea& activeArea, float resourceCircleRadius, WorldEntityKey sourceEntityKey,
            float headPositionX, float headPositionY, const BodyTrailComponent& bodyTrail, std::uint32_t growthPoint,
            const WorldResourceRegistry& resourceRegistry,
            std::span<const WorldResourcePosition> reservedPositions) noexcept;
    };
} // namespace psnr::world
