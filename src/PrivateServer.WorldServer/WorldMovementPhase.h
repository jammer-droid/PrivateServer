#pragma once

#include "WorldMovementPhaseResult.h"
#include "WorldMovementTickInput.h"
#include "WorldReadView.h"

#include <cstdint>
#include <span>

namespace psnr::world
{
    enum class WorldMovementPhaseComputeResult : std::uint8_t
    {
        Computed = 0,
        InvalidArgument,
        EntityStateInvariantViolation,
    };

    // 입력과 동일한 위치의 caller-owned 결과 span에만 기록한다.
    // 동일한 read view를 공유하며 입력/결과를 같은 경계로 나누면 actor/job 단위 subspan 실행이 가능하다.
    class WorldMovementPhase final
    {
    public:
        [[nodiscard]] static WorldMovementPhaseComputeResult Compute(
            std::span<const WorldMovementTickInput> inputs, float fixedDeltaSeconds, const WorldReadView& readView,
            std::span<WorldMovementEntityUpdate> outUpdates) noexcept;
    };
} // namespace psnr::world
