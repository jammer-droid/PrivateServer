#pragma once

#include "WorldEntityComponents.h"
#include "WorldEntityIdentity.h"
#include "WorldSessionRegistry.h"

#include <cstdint>

namespace psnr::world
{
    // ControlStateCommand payload를 admission한 뒤 World 내부에서 보관하는 owned command다.
    // Player와 entity identity는 packet이 아니라 현재 session binding에서 가져온다.
    struct WorldControlCommand final
    {
        WorldSessionKey sessionKey{};
        std::uint32_t playerId = 0;
        WorldEntityKey entityKey{};
        std::uint32_t admittedServerTick = 0;
        std::uint32_t inputSequence = 0;
        WorldTurnState turnState = WorldTurnState::Straight;
        WorldBoostState boostState = WorldBoostState::Off;

        [[nodiscard]] friend bool operator==(const WorldControlCommand& left,
                                             const WorldControlCommand& right) noexcept = default;
    };
} // namespace psnr::world
