#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace psnr::world::protocol::v1
{
    struct MovementInput final
    {
        /*
        0         2             6          10       12       14
        +---------+-------------+-----------+--------+--------+
        | version | generation  | targetTick| moveX  | moveY  |
        +---------+-------------+-----------+--------+--------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ControlledEntityGenerationOffset = 2;
            static constexpr std::size_t TargetServerTickOffset = 6;
            static constexpr std::size_t MoveXOffset = 10;
            static constexpr std::size_t MoveYOffset = 12;
            static constexpr std::size_t PayloadBytes = 14;
        };

        std::uint32_t controlledEntityGeneration = 0;
        std::uint32_t targetServerTick = 0;
        std::int16_t moveX = 0;
        std::int16_t moveY = 0;

        [[nodiscard]] static WorldProtocolError Encode(const MovementInput& value, std::span<std::byte> output) noexcept
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
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration, Wire::ControlledEntityGenerationOffset,
                                             output);
            WorldProtocolWireCodec::WriteU32(value.targetServerTick, Wire::TargetServerTickOffset, output);
            WorldProtocolWireCodec::WriteI16(value.moveX, Wire::MoveXOffset, output);
            WorldProtocolWireCodec::WriteI16(value.moveY, Wire::MoveYOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       MovementInput* outValue) noexcept
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

            MovementInput decoded;
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            decoded.targetServerTick = WorldProtocolWireCodec::ReadU32(Wire::TargetServerTickOffset, payload);
            decoded.moveX = WorldProtocolWireCodec::ReadI16(Wire::MoveXOffset, payload);
            decoded.moveY = WorldProtocolWireCodec::ReadI16(Wire::MoveYOffset, payload);
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const MovementInput& left,
                                                       const MovementInput& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsValid(const MovementInput& value) noexcept
        {
            return value.controlledEntityGeneration != 0 && value.moveX != std::numeric_limits<std::int16_t>::min() &&
                   value.moveY != std::numeric_limits<std::int16_t>::min();
        }
    };
} // namespace psnr::world::protocol::v1
