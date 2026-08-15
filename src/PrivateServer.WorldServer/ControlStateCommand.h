#pragma once

#include "WorldProtocolWireCodec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::protocol::v2
{
    struct ControlStateCommand final
    {
        /*
        0         2             6          10        12        14
        +---------+-------------+-----------+---------+---------+
        | version | generation  | sequence  | turn    | boost   |
        +---------+-------------+-----------+---------+---------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ControlledEntityGenerationOffset = 2;
            static constexpr std::size_t InputSequenceOffset = 6;
            static constexpr std::size_t TurnStateOffset = 10;
            static constexpr std::size_t BoostStateOffset = 12;
            static constexpr std::size_t PayloadBytes = 14;
        };

        std::uint32_t controlledEntityGeneration = 0;
        std::uint32_t inputSequence = 0;
        TurnState turnState = TurnState::Invalid;
        BoostState boostState = BoostState::Invalid;

        [[nodiscard]] static WorldProtocolError Encode(const ControlStateCommand& value,
                                                       std::span<std::byte> output) noexcept
        {
            if (output.size() != Wire::PayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!IsValidTurnState(value.turnState) || !IsValidBoostState(value.boostState))
            {
                return WorldProtocolError::InvalidEnum;
            }
            if (value.controlledEntityGeneration == 0 || value.inputSequence == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration,
                                             Wire::ControlledEntityGenerationOffset, output);
            WorldProtocolWireCodec::WriteU32(value.inputSequence, Wire::InputSequenceOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.turnState), Wire::TurnStateOffset,
                                             output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.boostState), Wire::BoostStateOffset,
                                             output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       ControlStateCommand* const outValue) noexcept
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

            const TurnState turnState =
                static_cast<TurnState>(WorldProtocolWireCodec::ReadU16(Wire::TurnStateOffset, payload));
            const BoostState boostState =
                static_cast<BoostState>(WorldProtocolWireCodec::ReadU16(Wire::BoostStateOffset, payload));
            if (!IsValidTurnState(turnState) || !IsValidBoostState(boostState))
            {
                return WorldProtocolError::InvalidEnum;
            }

            ControlStateCommand decoded;
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            decoded.inputSequence = WorldProtocolWireCodec::ReadU32(Wire::InputSequenceOffset, payload);
            decoded.turnState = turnState;
            decoded.boostState = boostState;
            if (decoded.controlledEntityGeneration == 0 || decoded.inputSequence == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const ControlStateCommand& left,
                                                       const ControlStateCommand& right) noexcept = default;

    private:
        [[nodiscard]] static constexpr bool IsValidTurnState(const TurnState value) noexcept
        {
            return value == TurnState::Straight || value == TurnState::Left || value == TurnState::Right;
        }

        [[nodiscard]] static constexpr bool IsValidBoostState(const BoostState value) noexcept
        {
            return value == BoostState::Off || value == BoostState::On;
        }
    };
} // namespace psnr::world::protocol::v2
