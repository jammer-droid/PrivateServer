#pragma once

#include "WorldEntityIdentity.h"

#include <cstdint>

namespace psnr::world
{
    inline constexpr std::uint32_t WorldPhysicsMaxContactsPerTick = 4;

    struct PhysicsFixtureId final
    {
        std::uint32_t value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const PhysicsFixtureId& left,
                                                       const PhysicsFixtureId& right) noexcept = default;
    };

    enum class PhysicsProxyBehavior : std::uint8_t
    {
        Invalid = 0,
        Solid,
        Trigger,
    };

    struct PhysicsCircleShape final // circle only in this project
    {
        float radius = 0.0f;

        [[nodiscard]] friend bool operator==(const PhysicsCircleShape& left,
                                             const PhysicsCircleShape& right) noexcept = default;
    };

    // Physics library handle을 포함하지 않는 World-owned collision/query value다.
    struct PhysicsProxy final
    {
        WorldEntityKey ownerKey{};
        EntityHandle ownerHandle{};
        PhysicsFixtureId fixtureId{};
        float localOffsetX = 0.0f;
        float localOffsetY = 0.0f;
        PhysicsCircleShape shape{}; // circle only
        std::uint32_t layer = 0;    // 내가 어떤 종류의 객체인지 나타내는 layer
        std::uint32_t mask = 0;     // 내가 어떤 종류의 객체와 상호작용할지 나타내는 mask
        PhysicsProxyBehavior behavior = PhysicsProxyBehavior::Invalid;

        [[nodiscard]] friend bool operator==(const PhysicsProxy& left, const PhysicsProxy& right) noexcept = default;
    };

    struct WorldPhysicsConfig final
    {
        std::uint32_t maxContactsPerTick = WorldPhysicsMaxContactsPerTick;
        float collisionEpsilon = 0.0f;
        float contactTolerance = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldPhysicsConfig& left,
                                             const WorldPhysicsConfig& right) noexcept = default;
    };

    struct WorldPhysicsArenaBounds final // world arena(world map) 경계(직사각형)
    {
        [[nodiscard]] static bool IsValid(const WorldPhysicsArenaBounds& bounds) noexcept;

        float minimumX = 0.0f;
        float minimumY = 0.0f;
        float maximumX = 0.0f;
        float maximumY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldPhysicsArenaBounds& left,
                                             const WorldPhysicsArenaBounds& right) noexcept = default;
    };

    // PhysicsProxy와 그것을 소유한 entity 의 현재 위치를 묶은 물리 계산용 snapshot
    struct WorldPhysicsProxyProjection final
    {
        // 원의 중심 = ownerPosition + proxy.localOffset
        PhysicsProxy proxy{};
        float ownerPositionX = 0.0f;
        float ownerPositionY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldPhysicsProxyProjection& left,
                                             const WorldPhysicsProxyProjection& right) noexcept = default;
    };

    // 이번 tick 에 실제로 움직일 entity 하나의 물리 입력
    struct WorldPhysicsMovementInput final
    {
        WorldPhysicsProxyProjection movingProxy{};
        float movementInputX = 0.0f;
        float movementInputY = 0.0f;
        float maxMoveSpeed = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldPhysicsMovementInput& left,
                                             const WorldPhysicsMovementInput& right) noexcept = default;
    };

    [[nodiscard]] bool IsValid(const PhysicsProxy& proxy) noexcept;
    [[nodiscard]] bool IsValid(const WorldPhysicsConfig& config) noexcept;
    [[nodiscard]] bool IsValid(const WorldPhysicsProxyProjection& projection) noexcept;
    [[nodiscard]] bool IsValid(const WorldPhysicsMovementInput& input) noexcept;
} // namespace psnr::world
