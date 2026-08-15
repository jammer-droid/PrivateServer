#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v1
{
    struct ScoreState final
    {
        /*
        0   2      6       10      14
        +---+------+-------+-------+
        |ver|tick  |player |score  |
        +---+------+-------+-------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t PlayerIdOffset = 6;
            static constexpr std::size_t ScoreOffset = 10;
            static constexpr std::size_t PayloadBytes = 14;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t playerId = 0;
        std::uint32_t score = 0;

        [[nodiscard]] static WorldProtocolError Encode(const ScoreState& value, std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (value.playerId == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.serverTick, Wire::ServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.playerId, Wire::PlayerIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.score, Wire::ScoreOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       ScoreState* outValue) noexcept
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

            ScoreState decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.playerId = WorldProtocolWireCodec::ReadU32(Wire::PlayerIdOffset, payload);
            decoded.score = WorldProtocolWireCodec::ReadU32(Wire::ScoreOffset, payload);
            if (decoded.playerId == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const ScoreState& left,
                                                       const ScoreState& right) noexcept = default;
    };
} // namespace psnr::world::protocol::v1
