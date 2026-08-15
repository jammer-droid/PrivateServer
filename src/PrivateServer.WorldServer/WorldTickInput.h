#pragma once

#include "WorldMovementTickInput.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace psnr::world
{
    // Runtime ingress와 packet별 builder에서 검증이 완료된 한 tick의 입력을 묶는 container다.
    // 입력을 다시 검증하지 않고 값으로 소유하며 phase에는 const view만 제공한다.
    class WorldTickInput final
    {
    public:
        WorldTickInput(std::uint32_t serverTick, std::vector<WorldMovementTickInput> movementInputs) noexcept
            : serverTick_(serverTick)
            , movementInputs_(std::move(movementInputs))
        {
        }

        [[nodiscard]] std::uint32_t ServerTick() const noexcept
        {
            return serverTick_;
        }

        [[nodiscard]] std::span<const WorldMovementTickInput> MovementInputs() const noexcept
        {
            return movementInputs_;
        }

    private:
        std::uint32_t serverTick_ = 0;
        std::vector<WorldMovementTickInput> movementInputs_;
    };
} // namespace psnr::world
