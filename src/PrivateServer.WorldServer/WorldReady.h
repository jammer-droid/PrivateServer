#pragma once

#include "WorldProtocolWireCodec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::world::protocol::v1
{
    struct WorldReady final
    {
        /*
        0    2       6        10       14      18       22       26       30       34       38       42       46
        +----+-------+--------+--------+-------+--------+--------+--------+--------+--------+--------+--------+
        | ver|player |entityId|entityGn|tick   |tickRate|snapInt |slack   |MinX    |MinY    |MaxX    |MaxY    |
        +----+-------+--------+--------+-------+--------+--------+--------+--------+--------+--------+--------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t PlayerIdOffset = 2;
            static constexpr std::size_t ControlledEntityIdOffset = 6;
            static constexpr std::size_t ControlledEntityGenerationOffset = 10;
            static constexpr std::size_t CurrentServerTickOffset = 14;
            static constexpr std::size_t TickRateHzOffset = 18;
            static constexpr std::size_t SnapshotIntervalTicksOffset = 22;
            static constexpr std::size_t CommandSlackTicksOffset = 26;
            static constexpr std::size_t ArenaMinXOffset = 30;
            static constexpr std::size_t ArenaMinYOffset = 34;
            static constexpr std::size_t ArenaMaxXOffset = 38;
            static constexpr std::size_t ArenaMaxYOffset = 42;
            static constexpr std::size_t PayloadBytes = 46;
        };

        std::uint32_t playerId = 0;
        std::uint32_t controlledEntityId = 0;
        std::uint32_t controlledEntityGeneration = 0;
        std::uint32_t currentServerTick = 0;
        std::uint32_t tickRateHz = 0;
        std::uint32_t snapshotIntervalTicks = 0;
        std::uint32_t commandSlackTicks = 0;
        float arenaMinX = 0.0f;
        float arenaMinY = 0.0f;
        float arenaMaxX = 0.0f;
        float arenaMaxY = 0.0f;

        [[nodiscard]] static WorldProtocolError Encode(const WorldReady& value, std::span<std::byte> output) noexcept
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
            WorldProtocolWireCodec::WriteU32(value.playerId, Wire::PlayerIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityId, Wire::ControlledEntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration, Wire::ControlledEntityGenerationOffset,
                                             output);
            WorldProtocolWireCodec::WriteU32(value.currentServerTick, Wire::CurrentServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.tickRateHz, Wire::TickRateHzOffset, output);
            WorldProtocolWireCodec::WriteU32(value.snapshotIntervalTicks, Wire::SnapshotIntervalTicksOffset, output);
            WorldProtocolWireCodec::WriteU32(value.commandSlackTicks, Wire::CommandSlackTicksOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMinX, Wire::ArenaMinXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMinY, Wire::ArenaMinYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMaxX, Wire::ArenaMaxXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMaxY, Wire::ArenaMaxYOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       WorldReady* outValue) noexcept
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

            WorldReady decoded;
            decoded.playerId = WorldProtocolWireCodec::ReadU32(Wire::PlayerIdOffset, payload);
            decoded.controlledEntityId = WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityIdOffset, payload);
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            decoded.currentServerTick = WorldProtocolWireCodec::ReadU32(Wire::CurrentServerTickOffset, payload);
            decoded.tickRateHz = WorldProtocolWireCodec::ReadU32(Wire::TickRateHzOffset, payload);
            decoded.snapshotIntervalTicks = WorldProtocolWireCodec::ReadU32(Wire::SnapshotIntervalTicksOffset, payload);
            decoded.commandSlackTicks = WorldProtocolWireCodec::ReadU32(Wire::CommandSlackTicksOffset, payload);
            decoded.arenaMinX = WorldProtocolWireCodec::ReadF32(Wire::ArenaMinXOffset, payload);
            decoded.arenaMinY = WorldProtocolWireCodec::ReadF32(Wire::ArenaMinYOffset, payload);
            decoded.arenaMaxX = WorldProtocolWireCodec::ReadF32(Wire::ArenaMaxXOffset, payload);
            decoded.arenaMaxY = WorldProtocolWireCodec::ReadF32(Wire::ArenaMaxYOffset, payload);
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const WorldReady& left,
                                                       const WorldReady& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsValid(const WorldReady& value) noexcept
        {
            return value.playerId != 0 && value.controlledEntityId != 0 && value.controlledEntityGeneration != 0 &&
                   value.tickRateHz != 0 && value.snapshotIntervalTicks != 0 && value.commandSlackTicks != 0 &&
                   std::isfinite(value.arenaMinX) && std::isfinite(value.arenaMinY) && std::isfinite(value.arenaMaxX) &&
                   std::isfinite(value.arenaMaxY) && value.arenaMinX < value.arenaMaxX &&
                   value.arenaMinY < value.arenaMaxY;
        }
    };
} // namespace psnr::world::protocol::v1

namespace psnr::world::protocol::v2
{
    struct WorldReady final
    {
        struct Wire final
        {
            static constexpr std::size_t PlayerIdOffset = 2;
            static constexpr std::size_t ControlledEntityIdOffset = 6;
            static constexpr std::size_t ControlledEntityGenerationOffset = 10;
            static constexpr std::size_t CurrentServerTickOffset = 14;
            static constexpr std::size_t TickRateHzOffset = 18;
            static constexpr std::size_t SnapshotIntervalTicksOffset = 22;
            static constexpr std::size_t CommandSlackTicksOffset = 26;
            static constexpr std::size_t ArenaMinXOffset = 30;
            static constexpr std::size_t ArenaMinYOffset = 34;
            static constexpr std::size_t ArenaMaxXOffset = 38;
            static constexpr std::size_t ArenaMaxYOffset = 42;
            static constexpr std::size_t ChannelIdOffset = 46;
            static constexpr std::size_t DisplayNameByteCountOffset = 50;
            static constexpr std::size_t DisplayNameOffset = 52;
            static constexpr std::size_t MinimumPayloadBytes = DisplayNameOffset;
            static constexpr std::size_t MaximumPayloadBytes =
                DisplayNameOffset + WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes;
        };

        std::uint32_t playerId = 0;
        std::uint32_t controlledEntityId = 0;
        std::uint32_t controlledEntityGeneration = 0;
        std::uint32_t currentServerTick = 0;
        std::uint32_t tickRateHz = 0;
        std::uint32_t snapshotIntervalTicks = 0;
        std::uint32_t commandSlackTicks = 0;
        float arenaMinX = 0.0f;
        float arenaMinY = 0.0f;
        float arenaMaxX = 0.0f;
        float arenaMaxY = 0.0f;
        std::uint32_t channelId = 0;
        std::string displayName;

        [[nodiscard]] static std::size_t CalculatePayloadBytes(const std::string_view displayName) noexcept
        {
            return displayName.size() <= WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes
                       ? Wire::DisplayNameOffset + displayName.size()
                       : 0;
        }

        [[nodiscard]] static WorldProtocolError Encode(const WorldReady& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t payloadBytes = CalculatePayloadBytes(value.displayName);
            if (payloadBytes == 0 || output.size() != payloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!IsValidNumeric(value))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(value.displayName))
            {
                return WorldProtocolError::InvalidArgument;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, 0, output);
            WorldProtocolWireCodec::WriteU32(value.playerId, Wire::PlayerIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityId, Wire::ControlledEntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration,
                                             Wire::ControlledEntityGenerationOffset, output);
            WorldProtocolWireCodec::WriteU32(value.currentServerTick, Wire::CurrentServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.tickRateHz, Wire::TickRateHzOffset, output);
            WorldProtocolWireCodec::WriteU32(value.snapshotIntervalTicks, Wire::SnapshotIntervalTicksOffset, output);
            WorldProtocolWireCodec::WriteU32(value.commandSlackTicks, Wire::CommandSlackTicksOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMinX, Wire::ArenaMinXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMinY, Wire::ArenaMinYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMaxX, Wire::ArenaMaxXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.arenaMaxY, Wire::ArenaMaxYOffset, output);
            WorldProtocolWireCodec::WriteU32(value.channelId, Wire::ChannelIdOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.displayName.size()),
                                             Wire::DisplayNameByteCountOffset, output);
            for (std::size_t index = 0; index < value.displayName.size(); ++index)
            {
                output[Wire::DisplayNameOffset + index] = static_cast<std::byte>(value.displayName[index]);
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       WorldReady* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::MinimumPayloadBytes || payload.size() > Wire::MaximumPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(0, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }
            const std::uint16_t displayNameByteCount =
                WorldProtocolWireCodec::ReadU16(Wire::DisplayNameByteCountOffset, payload);
            if (displayNameByteCount != payload.size() - Wire::DisplayNameOffset)
            {
                return WorldProtocolError::InvalidLength;
            }
            const std::span<const std::byte> displayNameBytes = payload.subspan(Wire::DisplayNameOffset);
            if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(displayNameBytes))
            {
                return WorldProtocolError::InvalidArgument;
            }

            WorldReady decoded;
            decoded.playerId = WorldProtocolWireCodec::ReadU32(Wire::PlayerIdOffset, payload);
            decoded.controlledEntityId = WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityIdOffset, payload);
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            decoded.currentServerTick = WorldProtocolWireCodec::ReadU32(Wire::CurrentServerTickOffset, payload);
            decoded.tickRateHz = WorldProtocolWireCodec::ReadU32(Wire::TickRateHzOffset, payload);
            decoded.snapshotIntervalTicks = WorldProtocolWireCodec::ReadU32(Wire::SnapshotIntervalTicksOffset, payload);
            decoded.commandSlackTicks = WorldProtocolWireCodec::ReadU32(Wire::CommandSlackTicksOffset, payload);
            decoded.arenaMinX = WorldProtocolWireCodec::ReadF32(Wire::ArenaMinXOffset, payload);
            decoded.arenaMinY = WorldProtocolWireCodec::ReadF32(Wire::ArenaMinYOffset, payload);
            decoded.arenaMaxX = WorldProtocolWireCodec::ReadF32(Wire::ArenaMaxXOffset, payload);
            decoded.arenaMaxY = WorldProtocolWireCodec::ReadF32(Wire::ArenaMaxYOffset, payload);
            decoded.channelId = WorldProtocolWireCodec::ReadU32(Wire::ChannelIdOffset, payload);
            decoded.displayName.reserve(displayNameBytes.size());
            for (const std::byte character : displayNameBytes)
            {
                decoded.displayName.push_back(static_cast<char>(std::to_integer<std::uint8_t>(character)));
            }
            if (!IsValidNumeric(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const WorldReady&, const WorldReady&) noexcept = default;

    private:
        [[nodiscard]] static bool IsValidNumeric(const WorldReady& value) noexcept
        {
            return value.playerId != 0 && value.controlledEntityId != 0 && value.controlledEntityGeneration != 0 &&
                   value.tickRateHz != 0 && value.snapshotIntervalTicks != 0 && value.commandSlackTicks != 0 &&
                   value.channelId != 0 && std::isfinite(value.arenaMinX) && std::isfinite(value.arenaMinY) &&
                   std::isfinite(value.arenaMaxX) && std::isfinite(value.arenaMaxY) &&
                   value.arenaMinX < value.arenaMaxX && value.arenaMinY < value.arenaMaxY;
        }
    };
} // namespace psnr::world::protocol::v2
