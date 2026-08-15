#pragma once

#include "WorldPhysicsValues.h"

#include <cstdint>
#include <span>
#include <vector>

namespace psnr::world
{
    struct PhysicsProxyKey final // proxy 식별
    {
        WorldEntityKey ownerKey{};    // proxy 를 소유한 world entity 와 generation
        PhysicsFixtureId fixtureId{}; // 해당 entity 안에서 proxy 를 구분하는 id

        [[nodiscard]] friend bool operator==(const PhysicsProxyKey& left,
                                             const PhysicsProxyKey& right) noexcept = default;
    };

    enum class PhysicsColliderKind : std::uint8_t // 움직이는 proxy 가 무엇과 충돌했는지 구분
    {
        Invalid = 0,
        ArenaBoundary, // arena 벽
        EntityProxy,   // 다른 physics proxy
    };

    enum class PhysicsArenaBoundary : std::uint8_t // arena boundray
    {
        Invalid = 0,
        Left,
        Bottom,
        Right,
        Top,
    };

    struct PhysicsColliderKey final // 충돌 대상을 표현하는 값 객체
    {
        PhysicsColliderKind kind = PhysicsColliderKind::Invalid;
        PhysicsArenaBoundary arenaBoundary = PhysicsArenaBoundary::Invalid; // kind == ArenaBoundray
        PhysicsProxyKey entityProxy{};                                      // kind == EntityProxy

        [[nodiscard]] friend bool operator==(const PhysicsColliderKey& left,
                                             const PhysicsColliderKey& right) noexcept = default;
    };

    struct WorldResolvedMotion final // moving entity 의 최종 이동 결과
    {
        WorldEntityKey ownerKey{};  // entity lifetime
        EntityHandle ownerHandle{}; // registry handle

        float positionX = 0.0f;
        float positionY = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;

        bool contactCapReached = false; // 한 tick 최대 4 contact 모두 사용 여부

        [[nodiscard]] friend bool operator==(const WorldResolvedMotion& left,
                                             const WorldResolvedMotion& right) noexcept = default;
    };

    // physics phase 가 sweep-and-slide 를 수행하면서 실제로 선택해 처리한 solid contact
    // sweep-and-slide : 목표 위치로 바로 이동하지 않고, 경로를 훑어서 첫 충돌까지만 이동, 남은 이동은 표면을 따라 미끄러짐
    // 1.이동 경로 계산
    //      - 현재 위치, 입력 방향, 최대 속도, fixed delta 를 알고 있으므로, 목표 이동량 계산 가능
    //      - 여기에서 시작점과 목표점이 정해짐
    // 2. 충돌 후보 수집(broad phase)
    //      - 움직이는 circle 이 지나가는 영역을 감싸는 AABB 생성
    //      - AABB와 겹칠 가능성이 있는 객체를 가져옴
    // 3. 후보별 충돌 계산(narrow phase)
    //      - AABB 에 들어왔다고 실제 충돌하는 것이 아니기 때문에 정확한 계산을 통해 timeOfImpact 계산
    //      - 가장 작은 값을 구한다
    // 4. slide 후 다시 후보 계산
    struct WorldPhysicsContact final
    {
        WorldEntityKey movingOwnerKey{};  // 움직이던 entity
        std::uint32_t contactOrdinal = 0; // 해당 entity 의 이번 tick 내 접촉 순서
        PhysicsColliderKey collider{};    // 벽 또는 다른 proxy 와 충돌했는지

        // [0.0 ~ 1.0]
        float timeOfImpact = 0.0f; // contact 가 발생한 시점(이번 tick 의 이동 중 n% 지점에서 충돌)
        float normalX = 0.0f;
        float normalY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldPhysicsContact& left,
                                             const WorldPhysicsContact& right) noexcept = default;
    };

    struct WorldTriggerOverlap final // 최종 위치에서 겹쳐있는 trigger 관련 proxy 쌍
    {
        PhysicsProxyKey first{};
        PhysicsProxyKey second{};

        [[nodiscard]] friend bool operator==(const WorldTriggerOverlap& left,
                                             const WorldTriggerOverlap& right) noexcept = default;
    };

    // Physics compute 결과를 값으로 소유한다. World owner가 generation/handle을 재검증한 뒤 commit한다.
    // 이번 tick에서 physics phase 가 처리한 전체 entity 의 결과를 묶어 보관하는 객체
    class WorldPhysicsStepResult final
    {
    public:
        WorldPhysicsStepResult() = default;
        WorldPhysicsStepResult(std::vector<WorldResolvedMotion> resolvedMotions,
                               std::vector<WorldPhysicsContact> contacts,
                               std::vector<WorldTriggerOverlap> triggerOverlaps) noexcept;

        [[nodiscard]] std::span<const WorldResolvedMotion> ResolvedMotions() const noexcept;
        [[nodiscard]] std::span<const WorldPhysicsContact> Contacts() const noexcept;
        [[nodiscard]] std::span<const WorldTriggerOverlap> TriggerOverlaps() const noexcept;

    private:
        std::vector<WorldResolvedMotion> resolvedMotions_;
        std::vector<WorldPhysicsContact> contacts_;
        std::vector<WorldTriggerOverlap> triggerOverlaps_;
    };
} // namespace psnr::world
