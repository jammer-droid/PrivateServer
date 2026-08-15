#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <span>

namespace psnr::world::protocol::v1
{
    struct ObserveWorldRequest final
    {
        /*
        0         2
        +---------+
        | version |
        +---------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t PayloadBytes = 2;
        };

        [[nodiscard]] static WorldProtocolError Encode(const ObserveWorldRequest&,
                                                       const std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       ObserveWorldRequest* const outValue) noexcept
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

            *outValue = ObserveWorldRequest{};
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const ObserveWorldRequest&,
                                                       const ObserveWorldRequest&) noexcept = default;
    };
} // namespace psnr::world::protocol::v1
