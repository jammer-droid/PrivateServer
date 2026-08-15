#pragma once

#include "WorldExecutionModeConfig.h"
#include "WorldIngressAdmission.h"
#include "WorldMovementCommand.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world
{
    class WorldMovementCommandAdmission final
    {
    public:
        [[nodiscard]] static WorldIngressAdmissionResult Admit(const WorldIngressAdmissionContext& context,
                                                               WorldInboundMode inboundMode,
                                                               std::uint32_t currentServerTick,
                                                               std::span<const std::byte> payload,
                                                               WorldMovementCommand* outCommand) noexcept;

        [[nodiscard]] static WorldIngressAdmissionResult Admit(const WorldIngressAdmissionContext& context,
                                                               std::uint32_t currentServerTick,
                                                               std::span<const std::byte> payload,
                                                               WorldMovementCommand* outCommand) noexcept;
    };
} // namespace psnr::world
