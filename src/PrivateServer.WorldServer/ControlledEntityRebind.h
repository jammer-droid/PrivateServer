#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v1
{
    // 같은 player/session이 제어하는 entity가 교체됐음을 old key -> new key 전환으로 전달한다.
    struct ControlledEntityRebind final
    {
        /*
        0   2      6       10       14      18       22       26
        +---+------+--------+--------+-------+--------+--------+
        |ver|tick  |playerId|prev Id |prevGen|new Id  |new Gen |
        +---+------+--------+--------+-------+--------+--------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t PlayerIdOffset = 6;
            static constexpr std::size_t PreviousEntityIdOffset = 10;
            static constexpr std::size_t PreviousEntityGenerationOffset = 14;
            static constexpr std::size_t ControlledEntityIdOffset = 18;
            static constexpr std::size_t ControlledEntityGenerationOffset = 22;
            static constexpr std::size_t PayloadBytes = 26;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t playerId = 0;
        std::uint32_t previousEntityId = 0;
        std::uint32_t previousEntityGeneration = 0;
        std::uint32_t controlledEntityId = 0;
        std::uint32_t controlledEntityGeneration = 0;

        [[nodiscard]] static WorldProtocolError Encode(const ControlledEntityRebind& value,
                                                       std::span<std::byte> output) noexcept
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
            WorldProtocolWireCodec::WriteU32(value.serverTick, Wire::ServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.playerId, Wire::PlayerIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.previousEntityId, Wire::PreviousEntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.previousEntityGeneration, Wire::PreviousEntityGenerationOffset,
                                             output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityId, Wire::ControlledEntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration, Wire::ControlledEntityGenerationOffset,
                                             output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       ControlledEntityRebind* const outValue) noexcept
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

            ControlledEntityRebind decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.playerId = WorldProtocolWireCodec::ReadU32(Wire::PlayerIdOffset, payload);
            decoded.previousEntityId = WorldProtocolWireCodec::ReadU32(Wire::PreviousEntityIdOffset, payload);
            decoded.previousEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::PreviousEntityGenerationOffset, payload);
            decoded.controlledEntityId = WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityIdOffset, payload);
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const ControlledEntityRebind& left,
                                                       const ControlledEntityRebind& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsValid(const ControlledEntityRebind& value) noexcept
        {
            return value.playerId != 0 && value.previousEntityId != 0 && value.previousEntityGeneration != 0 &&
                   value.controlledEntityId != 0 && value.controlledEntityGeneration != 0 &&
                   (value.previousEntityId != value.controlledEntityId ||
                    value.previousEntityGeneration != value.controlledEntityGeneration);
        }
    };
} // namespace psnr::world::protocol::v1
