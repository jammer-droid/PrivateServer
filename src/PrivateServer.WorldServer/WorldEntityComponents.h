#pragma once

#include "WorldBodyTrailComponent.h"

#include <cstdint>
#include <vector>

namespace psnr::world
{
    enum class WorldEntityKind : std::uint16_t
    {
        Invalid = 0,
        Player = 1,
        Resource = 2,
        StaticObstacle = 3,
    };

    enum class WorldShapeKind : std::uint16_t
    {
        Invalid = 0,
        Circle = 1,
    };

    enum class WorldTurnState : std::uint8_t
    {
        Straight = 0,
        Left,
        Right,
    };

    enum class WorldBoostState : std::uint8_t
    {
        Off = 0,
        On,
    };

    struct TransformComponent final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        float angleRadians = 0.0f;

        [[nodiscard]] friend bool operator==(const TransformComponent& left,
                                             const TransformComponent& right) noexcept = default;
    };

    struct MotionComponent final
    {
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float movementIntentX = 0.0f; // 정규화된 클라이언트 X 축 이동값
        float movementIntentY = 0.0f; // 정규화된 클라이언트 Y 축 이동값

        [[nodiscard]] friend bool operator==(const MotionComponent& left,
                                             const MotionComponent& right) noexcept = default;
    };

    struct MovementCapabilityComponent final
    {
        float maxMoveSpeed = 0.0f;

        [[nodiscard]] friend bool operator==(const MovementCapabilityComponent& left,
                                             const MovementCapabilityComponent& right) noexcept = default;
    };

    struct ReplicationMetadataComponent final // world entity 를 클라이언트에 spawn 하기 위한 복제 정보
    {
        WorldEntityKind entityKind = WorldEntityKind::Invalid;
        std::uint32_t archetypeId = 0;
        WorldShapeKind primaryShapeKind = WorldShapeKind::Invalid;
        float primaryCircleRadius = 0.0f;

        [[nodiscard]] friend bool operator==(const ReplicationMetadataComponent& left,
                                             const ReplicationMetadataComponent& right) noexcept = default;
    };

    struct PlayerControlComponent final
    {
        std::uint32_t playerId = 0; // 0 == player control 없음
        std::uint32_t lastInputSequence = 0;
        WorldTurnState turnState = WorldTurnState::Straight;
        WorldBoostState boostState = WorldBoostState::Off;

        [[nodiscard]] friend bool operator==(const PlayerControlComponent& left,
                                             const PlayerControlComponent& right) noexcept = default;
    };

    struct PhysicsProxyId final // Physics 처리에 필요한 collision object 식별자(충돌, 트리거 등)
    {
        std::uint32_t value = 0; // 0 == invalid

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const PhysicsProxyId& left,
                                                       const PhysicsProxyId& right) noexcept = default;
    };

    struct PhysicsBindingComponent final // 하나의 world entity 가 소유한 PhysicsProxyId 보관 컨테이너
    {
        std::vector<PhysicsProxyId> proxyIds;

        [[nodiscard]] friend bool operator==(const PhysicsBindingComponent& left,
                                             const PhysicsBindingComponent& right) noexcept = default;
    };

    // Entity 하나에 필요한 최소 component 묶음이다.
    // Store는 EntityHandle.slotIndex를 통해 이 묶음을 찾고 tick commit 단위로 교체한다.
    struct WorldEntityComponents final
    {
        TransformComponent transform;                     // 위치
        MotionComponent motion;                           // 현재 vector, 입력값
        MovementCapabilityComponent movementCapability;   // 이동속도
        ReplicationMetadataComponent replicationMetadata; // spawn 정보
        PlayerControlComponent playerControl;             // id
        BodyTrailComponent bodyTrail;                     // head에서 tail 방향의 중심선 sample
        PhysicsBindingComponent physicsBinding;           //

        [[nodiscard]] friend bool operator==(const WorldEntityComponents& left,
                                             const WorldEntityComponents& right) noexcept = default;
    };
} // namespace psnr::world
