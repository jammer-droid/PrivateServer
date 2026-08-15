#pragma once

#include "WorldEntityIdentity.h"
#include "WorldSessionRegistry.h"

#include <cstdint>

namespace psnr::world
{
    inline constexpr std::uint32_t MaxInputHoldTicks = 2; // 신규 command 가 없어도 기존 입력값을 유지하는 최대 tick

    // 한 World tick에서 controlled entity에 적용할 movement input이다.
    struct WorldMovementTickInput final
    {
        WorldSessionKey sessionKey{};
        std::uint32_t playerId = 0;
        WorldEntityKey entityKey{};
        float movementInputX = 0.0f;
        float movementInputY = 0.0f;
        bool usesControlMovement = false;
        float headingRadians = 0.0f;
        float moveSpeed = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldMovementTickInput& left,
                                             const WorldMovementTickInput& right) noexcept = default;
    };
} // namespace psnr::world
