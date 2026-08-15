#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v1
{
    struct RoundState final
    {
        /*
        0   2      6       10    12         16       20       24
        +---+------+-------+-----+----------+--------+--------+
        |ver|tick  |roundId|phase|phaseEnd  |scoreWin|winner  |
        +---+------+-------+-----+----------+--------+--------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t RoundIdOffset = 6;
            static constexpr std::size_t PhaseOffset = 10;
            static constexpr std::size_t PhaseEndsAtServerTickOffset = 12;
            static constexpr std::size_t ScoreToWinOffset = 16;
            static constexpr std::size_t WinnerPlayerIdOffset = 20;
            static constexpr std::size_t PayloadBytes = 24;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t roundId = 0;
        RoundPhase phase = RoundPhase::Invalid;
        std::uint32_t phaseEndsAtServerTick = 0;
        std::uint32_t scoreToWin = 0;
        std::uint32_t winnerPlayerId = 0;

        [[nodiscard]] static WorldProtocolError Encode(const RoundState& value, std::span<std::byte> output) noexcept
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
            WorldProtocolWireCodec::WriteU32(value.roundId, Wire::RoundIdOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.phase), Wire::PhaseOffset, output);
            WorldProtocolWireCodec::WriteU32(value.phaseEndsAtServerTick, Wire::PhaseEndsAtServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.scoreToWin, Wire::ScoreToWinOffset, output);
            WorldProtocolWireCodec::WriteU32(value.winnerPlayerId, Wire::WinnerPlayerIdOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       RoundState* outValue) noexcept
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

            RoundState decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.roundId = WorldProtocolWireCodec::ReadU32(Wire::RoundIdOffset, payload);
            decoded.phase = static_cast<RoundPhase>(WorldProtocolWireCodec::ReadU16(Wire::PhaseOffset, payload));
            decoded.phaseEndsAtServerTick = WorldProtocolWireCodec::ReadU32(Wire::PhaseEndsAtServerTickOffset, payload);
            decoded.scoreToWin = WorldProtocolWireCodec::ReadU32(Wire::ScoreToWinOffset, payload);
            decoded.winnerPlayerId = WorldProtocolWireCodec::ReadU32(Wire::WinnerPlayerIdOffset, payload);
            const WorldProtocolError validationError = Validate(decoded);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const RoundState& left,
                                                       const RoundState& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsKnown(const RoundPhase value) noexcept
        {
            return value == RoundPhase::Waiting || value == RoundPhase::Running || value == RoundPhase::Ended;
        }

        [[nodiscard]] static WorldProtocolError Validate(const RoundState& value) noexcept
        {
            if (!IsKnown(value.phase))
            {
                return WorldProtocolError::InvalidEnum;
            }
            if (value.roundId == 0 || value.scoreToWin == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }
            if (value.phase == RoundPhase::Ended)
            {
                return value.winnerPlayerId == 0 ? WorldProtocolError::InvalidNumeric : WorldProtocolError::Success;
            }

            return value.winnerPlayerId == 0 ? WorldProtocolError::Success : WorldProtocolError::InvalidNumeric;
        }
    };
} // namespace psnr::world::protocol::v1
