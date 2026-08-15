#pragma once

#include "ControlStateCommand.h"
#include "WorldControlCommand.h"
#include "WorldIngressAdmission.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world
{
    class WorldControlCommandAdmission final
    {
    public:
        [[nodiscard]] static WorldIngressAdmissionResult Admit(const WorldIngressAdmissionContext& context,
                                                               std::uint32_t currentServerTick,
                                                               std::span<const std::byte> payload,
                                                               WorldControlCommand* outCommand) noexcept;

    private:
        [[nodiscard]] static WorldTurnState ToWorldTurnState(protocol::v2::TurnState value) noexcept;
        [[nodiscard]] static WorldBoostState ToWorldBoostState(protocol::v2::BoostState value) noexcept;
    };
} // namespace psnr::world
