#pragma once

#include "WorldEntityComponents.h"
#include "WorldEntityIdentity.h"

#include <span>
#include <utility>
#include <vector>

namespace psnr::world
{
    // Movement phase가 계산한 entity별 변경값이다.(TransformComponent, MotionComponent only)
    // 다른 component는 commit 대상에 포함하지 않아 phase의 변경 범위를 제한한다.
    struct WorldMovementEntityUpdate final
    {
        WorldEntityKey entityKey{};
        TransformComponent transform;
        MotionComponent motion;

        [[nodiscard]] friend bool operator==(const WorldMovementEntityUpdate& left,
                                             const WorldMovementEntityUpdate& right) noexcept = default;
    };

    // Movement phase의 계산 결과를 값으로 소유하는 immutable container다.
    // update는 WorldEntityKey 순으로 보관해 owner commit 순서를 입력 생성 순서와 분리한다.
    class WorldMovementPhaseResult final
    {
    public:
        explicit WorldMovementPhaseResult(std::vector<WorldMovementEntityUpdate> updates);

        [[nodiscard]] std::span<const WorldMovementEntityUpdate> Updates() const noexcept;

    private:
        std::vector<WorldMovementEntityUpdate> updates_;
    };
} // namespace psnr::world
