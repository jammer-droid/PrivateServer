#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace psnr::world::protocol::v2
{
    struct RoundResult final
    {
        struct Wire final
        {
            /*
            0   2      6       10       14          18      20             20+4N
            +---+------+-------+--------+-----------+-------+--------------+
            |ver|endTick|roundId|winning|recipient  |count  |winner IDs... |
            +---+------+-------+--------+-----------+-------+--------------+
            */
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t EndTickOffset = 2;
            static constexpr std::size_t RoundIdOffset = 6;
            static constexpr std::size_t WinningGrowthPointOffset = 10;
            static constexpr std::size_t RecipientFinalGrowthPointOffset = 14;
            static constexpr std::size_t WinnerCountOffset = 18;
            static constexpr std::size_t HeaderBytes = 20;
            static constexpr std::size_t WinnerPlayerIdBytes = 4;
            static constexpr std::size_t MaximumPayloadBytes = 8186;
            static constexpr std::size_t MaximumWinnerCount =
                (MaximumPayloadBytes - HeaderBytes) / WinnerPlayerIdBytes;

            [[nodiscard]] static constexpr std::size_t CalculatePayloadBytes(
                const std::size_t winnerCount) noexcept
            {
                return winnerCount <= MaximumWinnerCount ? HeaderBytes + winnerCount * WinnerPlayerIdBytes : 0;
            }
        };

        std::uint32_t endTick = 0;
        std::uint32_t roundId = 0;
        std::uint32_t winningGrowthPoint = 0;
        std::uint32_t recipientFinalGrowthPoint = 0;
        std::vector<std::uint32_t> winnerPlayerIds;

        [[nodiscard]] static WorldProtocolError Encode(const RoundResult& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t payloadBytes = Wire::CalculatePayloadBytes(value.winnerPlayerIds.size());
            if (payloadBytes == 0 || output.size() != payloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (Validate(value) != WorldProtocolError::Success)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.endTick, Wire::EndTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.roundId, Wire::RoundIdOffset, output);
            WorldProtocolWireCodec::WriteU32(value.winningGrowthPoint, Wire::WinningGrowthPointOffset, output);
            WorldProtocolWireCodec::WriteU32(value.recipientFinalGrowthPoint,
                                             Wire::RecipientFinalGrowthPointOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.winnerPlayerIds.size()),
                                             Wire::WinnerCountOffset, output);
            std::size_t offset = Wire::HeaderBytes;
            for (const std::uint32_t winnerPlayerId : value.winnerPlayerIds)
            {
                WorldProtocolWireCodec::WriteU32(winnerPlayerId, offset, output);
                offset += Wire::WinnerPlayerIdBytes;
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       RoundResult* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::HeaderBytes || payload.size() > Wire::MaximumPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(Wire::PayloadVersionOffset, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }

            const std::uint16_t winnerCount = WorldProtocolWireCodec::ReadU16(Wire::WinnerCountOffset, payload);
            if (Wire::CalculatePayloadBytes(winnerCount) != payload.size())
            {
                return WorldProtocolError::InvalidLength;
            }

            RoundResult decoded;
            decoded.endTick = WorldProtocolWireCodec::ReadU32(Wire::EndTickOffset, payload);
            decoded.roundId = WorldProtocolWireCodec::ReadU32(Wire::RoundIdOffset, payload);
            decoded.winningGrowthPoint = WorldProtocolWireCodec::ReadU32(Wire::WinningGrowthPointOffset, payload);
            decoded.recipientFinalGrowthPoint =
                WorldProtocolWireCodec::ReadU32(Wire::RecipientFinalGrowthPointOffset, payload);
            decoded.winnerPlayerIds.resize(winnerCount);
            std::size_t offset = Wire::HeaderBytes;
            for (std::size_t index = 0; index < winnerCount; ++index)
            {
                decoded.winnerPlayerIds[index] = WorldProtocolWireCodec::ReadU32(offset, payload);
                offset += Wire::WinnerPlayerIdBytes;
            }
            if (Validate(decoded) != WorldProtocolError::Success)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const RoundResult&, const RoundResult&) noexcept = default;

        [[nodiscard]] static WorldProtocolError Validate(const RoundResult& value) noexcept
        {
            if (value.endTick == 0 || value.roundId == 0 ||
                (value.winnerPlayerIds.empty() && value.winningGrowthPoint != 0))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            std::uint32_t previousPlayerId = 0;
            for (const std::uint32_t playerId : value.winnerPlayerIds)
            {
                if (playerId == 0 || playerId <= previousPlayerId)
                {
                    return WorldProtocolError::InvalidNumeric;
                }
                previousPlayerId = playerId;
            }
            return WorldProtocolError::Success;
        }
    };
} // namespace psnr::world::protocol::v2
