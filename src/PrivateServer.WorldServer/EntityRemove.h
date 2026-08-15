#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v1
{
    struct EntityRemove final
    {
        /*
        0   2      6       10      14      16
        +---+------+-------+-------+-------+
        |ver|tick  |entity |gen    |reason |
        +---+------+-------+-------+-------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t EntityIdOffset = 6;
            static constexpr std::size_t GenerationOffset = 10;
            static constexpr std::size_t ReasonOffset = 14;
            static constexpr std::size_t PayloadBytes = 16;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t entityId = 0;
        std::uint32_t generation = 0;
        EntityRemoveReason reason = EntityRemoveReason::Invalid;

        [[nodiscard]] static WorldProtocolError Encode(const EntityRemove& value, std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            const WorldProtocolError validationError = Validate(value);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.serverTick, Wire::ServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.entityId, Wire::EntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.generation, Wire::GenerationOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.reason), Wire::ReasonOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       EntityRemove* outValue) noexcept
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

            EntityRemove decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.entityId = WorldProtocolWireCodec::ReadU32(Wire::EntityIdOffset, payload);
            decoded.generation = WorldProtocolWireCodec::ReadU32(Wire::GenerationOffset, payload);
            decoded.reason =
                static_cast<EntityRemoveReason>(WorldProtocolWireCodec::ReadU16(Wire::ReasonOffset, payload));
            const WorldProtocolError validationError = Validate(decoded);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const EntityRemove& left,
                                                       const EntityRemove& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsKnown(const EntityRemoveReason value) noexcept
        {
            return value == EntityRemoveReason::LeftAoi || value == EntityRemoveReason::Destroyed ||
                   value == EntityRemoveReason::Collected || value == EntityRemoveReason::SessionClosed ||
                   value == EntityRemoveReason::RoundReset;
        }

    public:
        [[nodiscard]] static WorldProtocolError Validate(const EntityRemove& value) noexcept
        {
            if (!IsKnown(value.reason))
            {
                return WorldProtocolError::InvalidEnum;
            }
            if (value.entityId == 0 || value.generation == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            return WorldProtocolError::Success;
        }
    };
} // namespace psnr::world::protocol::v1
