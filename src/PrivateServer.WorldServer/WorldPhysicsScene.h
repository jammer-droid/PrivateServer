#pragma once

#include "WorldCollisionProxyBatch.h"
#include "WorldPhysicsStepResult.h"
#include "WorldResult.h"
#include "WorldPhysicsValues.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace psnr::world
{
    class WorldBox2dCollisionAdapter;

    // Canonical World state의 소유권과 commit 책임은 갖지 않는다.
    // World 의 물리 계산을 담당하는 작업 공간
    // 물리 계산 adapter 를 이용하는 외부 인터페이스 역할
    class WorldPhysicsScene final
    {
    public:
        WorldPhysicsScene(const WorldPhysicsScene&) = delete;
        WorldPhysicsScene& operator=(const WorldPhysicsScene&) = delete;
        ~WorldPhysicsScene() noexcept;

        [[nodiscard]] static WorldResult<std::unique_ptr<WorldPhysicsScene>> Create(
            const WorldPhysicsConfig& config) noexcept;

        [[nodiscard]] WorldResult<WorldPhysicsStepResult> Compute(
            std::span<const WorldPhysicsMovementInput> movementInputs,
            std::span<const WorldPhysicsProxyProjection> staticSolidProxies,
            std::span<const WorldPhysicsProxyProjection> triggerProxies, const WorldPhysicsArenaBounds& arenaBounds,
            float fixedDeltaSeconds) const noexcept;

        [[nodiscard]] WorldResult<std::vector<WorldCollisionContact>> QueryPlayerCollisions(
            std::span<const WorldCollisionProxy> proxies) noexcept;
        // 현재 persistent player tree와 같은 batch의 reservation을 query하고, 비어 있으면 AABB를 즉시 예약한다.
        // reservation은 다음 QueryPlayerCollisions synchronization에서 제거된다.
        [[nodiscard]] WorldPlayerSpawnReservationResult TryReservePlayerSpawnPlacement(
            const WorldPlayerSpawnBounds& bounds) noexcept;

        [[nodiscard]] const WorldPhysicsConfig& Config() const noexcept;

    private:
        explicit WorldPhysicsScene(const WorldPhysicsConfig& config);

        WorldPhysicsConfig config_{};
        std::unique_ptr<WorldBox2dCollisionAdapter> collisionAdapter_;
    };
} // namespace psnr::world
