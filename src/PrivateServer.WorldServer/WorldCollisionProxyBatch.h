#pragma once

#include "WorldEntityComponents.h"
#include "WorldEntityIdentity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    enum class WorldCollisionProxyRole : std::uint8_t
    {
        Invalid = 0,
        PlayerHead,
        PlayerBody,
    };

    struct WorldCollisionProxy final
    {
        WorldEntityKey ownerKey{};
        WorldCollisionProxyRole role = WorldCollisionProxyRole::Invalid;
        std::uint32_t segmentOrdinal = 0;
        float startX = 0.0f;
        float startY = 0.0f;
        float endX = 0.0f;
        float endY = 0.0f;
        float radius = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldCollisionProxy& left,
                                             const WorldCollisionProxy& right) noexcept = default;
    };

    // AABB Tree insert 용도
    struct WorldCollisionProxyKey final
    {
        WorldEntityKey ownerKey{};
        WorldCollisionProxyRole role = WorldCollisionProxyRole::Invalid;
        std::uint32_t segmentOrdinal = 0;

        [[nodiscard]] friend bool operator==(const WorldCollisionProxyKey& left,
                                             const WorldCollisionProxyKey& right) noexcept = default;
    };

    struct WorldCollisionContact final
    {
        WorldCollisionProxyKey first{};
        WorldCollisionProxyKey second{};

        [[nodiscard]] friend bool operator==(const WorldCollisionContact& left,
                                             const WorldCollisionContact& right) noexcept = default;
    };

    // Spawn candidate의 head와 body 전체를 감싸는 보수적 AABB다.
    struct WorldPlayerSpawnBounds final
    {
        [[nodiscard]] static bool IsValid(const WorldPlayerSpawnBounds& bounds) noexcept;

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldPlayerSpawnBounds& left,
                                             const WorldPlayerSpawnBounds& right) noexcept = default;
    };

    enum class WorldCollisionQueryResult : std::uint8_t
    {
        Queried = 0,
        InvalidArgument,
        InvalidInput,
        AllocationFailed,
    };

    enum class WorldPlayerSpawnReservationResult : std::uint8_t
    {
        Reserved = 0,
        Blocked,
        InvalidInput,
        AllocationFailed,
    };

    enum class WorldCollisionProxyAppendResult : std::uint8_t
    {
        Appended = 0,
        InvalidEntityState,
        CapacityExceeded,
        AllocationFailed,
    };

    // 한 tick 동안 Box2D physics 처리에 필요한 player head/body 오브젝트를
    // 하나의 vector에 누적하는 작업 buffer다. tick 시작에 Clear하고 확보된 capacity는 재사용한다.
    class WorldCollisionProxyBatch final
    {
    public:
        void Clear() noexcept;

        [[nodiscard]] WorldCollisionProxyAppendResult AppendPlayer(WorldEntityKey ownerKey,
                                                                   const WorldEntityComponents& components) noexcept;
        [[nodiscard]] std::span<const WorldCollisionProxy> Proxies() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t Capacity() const noexcept;

    private:
        std::vector<WorldCollisionProxy> proxies_;
    };
} // namespace psnr::world
