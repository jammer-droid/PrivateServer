#pragma once

#include <cstdint>

namespace psnr::world
{
    enum class WorldInboundMode : std::uint8_t
    {
        TargetServerTick = 0,
        DoubleBuffered = 1,
    };

    enum class WorldOutboundMode : std::uint8_t
    {
        Direct = 0,
        DoubleBuffered = 1,
    };

    struct WorldExecutionModeConfig final
    {
        WorldInboundMode inboundMode = WorldInboundMode::DoubleBuffered;
        WorldOutboundMode outboundMode = WorldOutboundMode::DoubleBuffered;
    };

    [[nodiscard]] constexpr bool IsValid(const WorldExecutionModeConfig& config) noexcept
    {
        const bool validInboundMode = config.inboundMode == WorldInboundMode::TargetServerTick ||
                                      config.inboundMode == WorldInboundMode::DoubleBuffered;
        const bool validOutboundMode = config.outboundMode == WorldOutboundMode::Direct ||
                                       config.outboundMode == WorldOutboundMode::DoubleBuffered;
        return validInboundMode && validOutboundMode;
    }
} // namespace psnr::world
