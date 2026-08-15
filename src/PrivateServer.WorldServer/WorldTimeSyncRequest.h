#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v1
{
    struct WorldTimeSyncRequest final
    {
        /*
        0         2             6
        +---------+-------------+
        | version | probeSeq    |
        +---------+-------------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ProbeSequenceOffset = 2;
            static constexpr std::size_t PayloadBytes = 6;
        };

        std::uint32_t probeSequence = 0;

        [[nodiscard]] static WorldProtocolError Encode(const WorldTimeSyncRequest& value,
                                                       std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.probeSequence, Wire::ProbeSequenceOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       WorldTimeSyncRequest* outValue) noexcept
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

            WorldTimeSyncRequest decoded;
            decoded.probeSequence = WorldProtocolWireCodec::ReadU32(Wire::ProbeSequenceOffset, payload);
            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const WorldTimeSyncRequest& left,
                                                       const WorldTimeSyncRequest& right) noexcept = default;
    };
} // namespace psnr::world::protocol::v1
