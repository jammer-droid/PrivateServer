#pragma once

#include "WorldEntityIdentity.h"
#include "WorldSessionRegistry.h"

#include <cstdint>

namespace psnr::world
{
    // WorldMovementCommand 는 client 가 실제로 보낸 요청이다.
    // MovementInput packet을 admission한 뒤 World 내부에서 보관하는 owned command다.
    // NrToWorldEvent의 raw payload view를 보관하지 않는다.
    struct WorldMovementCommand final
    {
        WorldSessionKey sessionKey{};
        std::uint32_t playerId = 0;
        WorldEntityKey entityKey{};
        std::uint32_t admittedServerTick = 0;
        std::uint32_t targetServerTick = 0;
        float movementInputX = 0.0f;
        float movementInputY = 0.0f;

        [[nodiscard]] friend bool operator==(const WorldMovementCommand& left,
                                             const WorldMovementCommand& right) noexcept = default;
    };
} // namespace psnr::world
