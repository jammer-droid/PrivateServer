#pragma once

#include "WorldCollisionProxyBatch.h"

#include <box2d/box2d.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace psnr::world
{
    // Box2D type과 dynamic-tree lifecycle을 WorldPhysicsScene 내부에 가두는 private adapter다.
    class WorldBox2dCollisionAdapter final
    {
    public:
        WorldBox2dCollisionAdapter();

        WorldBox2dCollisionAdapter(const WorldBox2dCollisionAdapter&) = delete;
        WorldBox2dCollisionAdapter& operator=(const WorldBox2dCollisionAdapter&) = delete;

        ~WorldBox2dCollisionAdapter() noexcept;

        [[nodiscard]] WorldCollisionQueryResult Query(std::span<const WorldCollisionProxy> proxies,
                                                      std::vector<WorldCollisionContact>* outContacts) noexcept;
        [[nodiscard]] WorldPlayerSpawnReservationResult TryReservePlayerSpawnPlacement(
            const WorldPlayerSpawnBounds& bounds) noexcept;

    private:
        struct ProxyKeyLess final
        {
            [[nodiscard]] bool operator()(const WorldCollisionProxyKey& left,
                                          const WorldCollisionProxyKey& right) const noexcept;
        };

        struct TreeProxyEntry final
        {
            WorldCollisionProxy proxy{};
            int treeProxyId = -1;
            std::uint64_t lastSeenGeneration = 0;
        };

        using TreeProxyMap = std::map<WorldCollisionProxyKey, TreeProxyEntry, ProxyKeyLess>;

        [[nodiscard]] WorldCollisionQueryResult Synchronize(std::span<const WorldCollisionProxy> proxies);
        void ClearPlayerSpawnReservations() noexcept;

        // Tree 자체는 scene lifetime 동안 유지하고 tick 입력과 달라진 leaf만 create/move/destroy한다.
        b2DynamicTree tree_{};
        TreeProxyMap treeProxies_;

        std::uint64_t syncGeneration_ = 0;
        std::vector<std::uint64_t> candidateUserData_;
        std::vector<int> playerSpawnReservationProxyIds_;
    };
} // namespace psnr::world
