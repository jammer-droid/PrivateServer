#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::world::protocol::v1
{
    struct JoinWorldRequest final
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

        [[nodiscard]] static WorldProtocolError Encode(const JoinWorldRequest&, std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       JoinWorldRequest* outValue) noexcept
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

            *outValue = JoinWorldRequest{};
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const JoinWorldRequest& left,
                                                       const JoinWorldRequest& right) noexcept = default;
    };
} // namespace psnr::world::protocol::v1

namespace psnr::world::protocol::v2
{
    struct JoinWorldRequest final
    {
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t DisplayNameByteCountOffset = 2;
            static constexpr std::size_t DisplayNameOffset = 4;
            static constexpr std::size_t MinimumPayloadBytes = DisplayNameOffset;
            static constexpr std::size_t MaximumPayloadBytes =
                DisplayNameOffset + WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes;
        };

        std::string displayName;

        [[nodiscard]] static std::size_t CalculatePayloadBytes(const std::string_view displayName) noexcept
        {
            return displayName.size() <= WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes
                       ? Wire::DisplayNameOffset + displayName.size()
                       : 0;
        }

        [[nodiscard]] static WorldProtocolError Encode(const JoinWorldRequest& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t payloadBytes = CalculatePayloadBytes(value.displayName);
            if (payloadBytes == 0 || output.size() != payloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(value.displayName))
            {
                return WorldProtocolError::InvalidArgument;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.displayName.size()),
                                             Wire::DisplayNameByteCountOffset, output);
            for (std::size_t index = 0; index < value.displayName.size(); ++index)
            {
                output[Wire::DisplayNameOffset + index] = static_cast<std::byte>(value.displayName[index]);
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       JoinWorldRequest* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::MinimumPayloadBytes || payload.size() > Wire::MaximumPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(Wire::PayloadVersionOffset, payload) != PayloadVersion)
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

            JoinWorldRequest decoded;
            decoded.displayName.reserve(displayNameBytes.size());
            for (const std::byte character : displayNameBytes)
            {
                decoded.displayName.push_back(static_cast<char>(std::to_integer<std::uint8_t>(character)));
            }
            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const JoinWorldRequest&, const JoinWorldRequest&) noexcept = default;
    };
} // namespace psnr::world::protocol::v2
