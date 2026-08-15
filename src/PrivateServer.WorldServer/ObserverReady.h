#pragma once

#include "WorldProtocolWireCodec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v1
{
    struct ObserverReady final
    {
        /*
        0    2       6       10      14      18      22      26      30
        +----+-------+-------+-------+-------+-------+-------+-------+
        | ver|tick   |tickHz |minX   |minY   |maxX   |maxY   |channel|
        +----+-------+-------+-------+-------+-------+-------+-------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t CurrentServerTickOffset = 2;
            static constexpr std::size_t TickRateHzOffset = 6;
            static constexpr std::size_t ArenaMinXOffset = 10;
            static constexpr std::size_t ArenaMinYOffset = 14;
            static constexpr std::size_t ArenaMaxXOffset = 18;
            static constexpr std::size_t ArenaMaxYOffset = 22;
            static constexpr std::size_t ChannelIdOffset = 26;
            static constexpr std::size_t PayloadBytes = 30;
        };

        std::uint32_t currentServerTick = 0;
        std::uint32_t tickRateHz = 0;
        float arenaMinX = 0.0f;
        float arenaMinY = 0.0f;
        float arenaMaxX = 0.0f;
        float arenaMaxY = 0.0f;
        std::uint32_t channelId = 0;

        [[nodiscard]] static WorldProtocolError Encode(const ObserverReady& value,
                                                       const std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!IsValid(value))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.currentServerTick, Wire::CurrentServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.tickRateHz, Wire::TickRateHzOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMinX, Wire::ArenaMinXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMinY, Wire::ArenaMinYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMaxX, Wire::ArenaMaxXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMaxY, Wire::ArenaMaxYOffset, output);
            WorldProtocolWireCodec::WriteU32(value.channelId, Wire::ChannelIdOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       ObserverReady* const outValue) noexcept
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(Wire::PayloadVersionOffset, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }

            ObserverReady decoded;
            decoded.currentServerTick = WorldProtocolWireCodec::ReadU32(Wire::CurrentServerTickOffset, payload);
            decoded.tickRateHz = WorldProtocolWireCodec::ReadU32(Wire::TickRateHzOffset, payload);
            decoded.arenaMinX = WorldProtocolWireCodec::ReadF32(Wire::ArenaMinXOffset, payload);
            decoded.arenaMinY = WorldProtocolWireCodec::ReadF32(Wire::ArenaMinYOffset, payload);
            decoded.arenaMaxX = WorldProtocolWireCodec::ReadF32(Wire::ArenaMaxXOffset, payload);
            decoded.arenaMaxY = WorldProtocolWireCodec::ReadF32(Wire::ArenaMaxYOffset, payload);
            decoded.channelId = WorldProtocolWireCodec::ReadU32(Wire::ChannelIdOffset, payload);
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const ObserverReady&, const ObserverReady&) noexcept = default;

    private:
        [[nodiscard]] static bool IsValid(const ObserverReady& value) noexcept
        {
            return value.tickRateHz != 0 && value.channelId != 0 && std::isfinite(value.arenaMinX) &&
                   std::isfinite(value.arenaMinY) && std::isfinite(value.arenaMaxX) && std::isfinite(value.arenaMaxY) &&
                   value.arenaMinX < value.arenaMaxX && value.arenaMinY < value.arenaMaxY;
        }
    };
} // namespace psnr::world::protocol::v1
