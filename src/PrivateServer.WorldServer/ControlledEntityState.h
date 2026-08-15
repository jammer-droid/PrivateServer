#pragma once

#include "WorldProtocolWireCodec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace psnr::world::protocol::v1
{
    struct ControlledEntityState final
    {
        /*
        0   2      6          10    14    18    22    26    30
        +---+------+----------+-----+-----+-----+-----+-----+
        |ver|tick  |generation|posX |posY |velX |velY |angle|
        +---+------+----------+-----+-----+-----+-----+-----+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t ControlledEntityGenerationOffset = 6;
            static constexpr std::size_t PositionXOffset = 10;
            static constexpr std::size_t PositionYOffset = 14;
            static constexpr std::size_t VelocityXOffset = 18;
            static constexpr std::size_t VelocityYOffset = 22;
            static constexpr std::size_t AngleRadiansOffset = 26;
            static constexpr std::size_t PayloadBytes = 30;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t controlledEntityGeneration = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float angleRadians = 0.0f;

        [[nodiscard]] static WorldProtocolError Encode(const ControlledEntityState& value,
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
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration, Wire::ControlledEntityGenerationOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(value.positionX, Wire::PositionXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.positionY, Wire::PositionYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.velocityX, Wire::VelocityXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.velocityY, Wire::VelocityYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.angleRadians, Wire::AngleRadiansOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       ControlledEntityState* outValue) noexcept
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

            ControlledEntityState decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            decoded.positionX = WorldProtocolWireCodec::ReadF32(Wire::PositionXOffset, payload);
            decoded.positionY = WorldProtocolWireCodec::ReadF32(Wire::PositionYOffset, payload);
            decoded.velocityX = WorldProtocolWireCodec::ReadF32(Wire::VelocityXOffset, payload);
            decoded.velocityY = WorldProtocolWireCodec::ReadF32(Wire::VelocityYOffset, payload);
            decoded.angleRadians = WorldProtocolWireCodec::ReadF32(Wire::AngleRadiansOffset, payload);
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const ControlledEntityState& left,
                                                       const ControlledEntityState& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsValid(const ControlledEntityState& value) noexcept
        {
            return value.controlledEntityGeneration != 0 && std::isfinite(value.positionX) &&
                   std::isfinite(value.positionY) && std::isfinite(value.velocityX) && std::isfinite(value.velocityY) &&
                   std::isfinite(value.angleRadians);
        }
    };
} // namespace psnr::world::protocol::v1

namespace psnr::world::protocol::v2
{
    struct ControlledEntityBodySample final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;

        [[nodiscard]] friend constexpr bool operator==(const ControlledEntityBodySample& left,
                                                       const ControlledEntityBodySample& right) noexcept = default;
    };

    struct ControlledEntityState final
    {
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t ControlledEntityGenerationOffset = 6;
            static constexpr std::size_t LastProcessedControlSequenceOffset = 10;
            static constexpr std::size_t HeadPositionXOffset = 14;
            static constexpr std::size_t HeadPositionYOffset = 18;
            static constexpr std::size_t HeadingRadiansOffset = 22;
            static constexpr std::size_t DiameterOffset = 26;
            static constexpr std::size_t GrowthPointOffset = 30;
            static constexpr std::size_t BoostStateOffset = 34;
            static constexpr std::size_t BodySampleCountOffset = 36;
            static constexpr std::size_t HeaderBytes = 38;
            static constexpr std::size_t BodySampleBytes = 8;
            static constexpr std::size_t MaximumPayloadBytes = 8186;
            static constexpr std::size_t MaximumBodySampleCount =
                (MaximumPayloadBytes - HeaderBytes) / BodySampleBytes;

            [[nodiscard]] static constexpr std::size_t CalculatePayloadBytes(
                const std::size_t bodySampleCount) noexcept
            {
                return bodySampleCount <= MaximumBodySampleCount
                           ? HeaderBytes + bodySampleCount * BodySampleBytes
                           : 0;
            }
        };

        std::uint32_t serverTick = 0;
        std::uint32_t controlledEntityGeneration = 0;
        std::uint32_t lastProcessedControlSequence = 0;
        float headPositionX = 0.0f;
        float headPositionY = 0.0f;
        float headingRadians = 0.0f;
        float diameter = 0.0f;
        std::uint32_t growthPoint = 0;
        BoostState boostState = BoostState::Invalid;
        std::vector<ControlledEntityBodySample> bodyTrailSamples;

        [[nodiscard]] static WorldProtocolError Encode(const ControlledEntityState& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t bodySampleCount = value.bodyTrailSamples.size();
            if (bodySampleCount > Wire::MaximumBodySampleCount ||
                output.size() != Wire::CalculatePayloadBytes(bodySampleCount))
            {
                return WorldProtocolError::InvalidLength;
            }
            if (value.boostState != BoostState::Off && value.boostState != BoostState::On)
            {
                return WorldProtocolError::InvalidEnum;
            }
            if (!IsValid(value))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU32(value.serverTick, Wire::ServerTickOffset, output);
            WorldProtocolWireCodec::WriteU32(value.controlledEntityGeneration,
                                             Wire::ControlledEntityGenerationOffset, output);
            WorldProtocolWireCodec::WriteU32(value.lastProcessedControlSequence,
                                             Wire::LastProcessedControlSequenceOffset, output);
            WorldProtocolWireCodec::WriteF32(value.headPositionX, Wire::HeadPositionXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.headPositionY, Wire::HeadPositionYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.headingRadians, Wire::HeadingRadiansOffset, output);
            WorldProtocolWireCodec::WriteF32(value.diameter, Wire::DiameterOffset, output);
            WorldProtocolWireCodec::WriteU32(value.growthPoint, Wire::GrowthPointOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.boostState), Wire::BoostStateOffset,
                                             output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(bodySampleCount),
                                             Wire::BodySampleCountOffset, output);
            for (std::size_t index = 0; index < bodySampleCount; ++index)
            {
                WriteBodySample(value.bodyTrailSamples[index], BodySampleOffset(index), output);
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       ControlledEntityState* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::HeaderBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(Wire::PayloadVersionOffset, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }

            const std::uint16_t bodySampleCount =
                WorldProtocolWireCodec::ReadU16(Wire::BodySampleCountOffset, payload);
            if (bodySampleCount > Wire::MaximumBodySampleCount ||
                payload.size() != Wire::CalculatePayloadBytes(bodySampleCount))
            {
                return WorldProtocolError::InvalidLength;
            }

            ControlledEntityState decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.controlledEntityGeneration =
                WorldProtocolWireCodec::ReadU32(Wire::ControlledEntityGenerationOffset, payload);
            decoded.lastProcessedControlSequence =
                WorldProtocolWireCodec::ReadU32(Wire::LastProcessedControlSequenceOffset, payload);
            decoded.headPositionX = WorldProtocolWireCodec::ReadF32(Wire::HeadPositionXOffset, payload);
            decoded.headPositionY = WorldProtocolWireCodec::ReadF32(Wire::HeadPositionYOffset, payload);
            decoded.headingRadians = WorldProtocolWireCodec::ReadF32(Wire::HeadingRadiansOffset, payload);
            decoded.diameter = WorldProtocolWireCodec::ReadF32(Wire::DiameterOffset, payload);
            decoded.growthPoint = WorldProtocolWireCodec::ReadU32(Wire::GrowthPointOffset, payload);
            decoded.boostState =
                static_cast<BoostState>(WorldProtocolWireCodec::ReadU16(Wire::BoostStateOffset, payload));
            if (decoded.boostState != BoostState::Off && decoded.boostState != BoostState::On)
            {
                return WorldProtocolError::InvalidEnum;
            }

            decoded.bodyTrailSamples.resize(bodySampleCount);
            for (std::size_t index = 0; index < bodySampleCount; ++index)
            {
                ReadBodySample(BodySampleOffset(index), payload, &decoded.bodyTrailSamples[index]);
            }
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const ControlledEntityState& left,
                                             const ControlledEntityState& right) noexcept = default;

    private:
        [[nodiscard]] static constexpr std::size_t BodySampleOffset(const std::size_t index) noexcept
        {
            return Wire::HeaderBytes + index * Wire::BodySampleBytes;
        }

        [[nodiscard]] static bool IsValid(const ControlledEntityBodySample& sample) noexcept
        {
            return std::isfinite(sample.positionX) && std::isfinite(sample.positionY);
        }

        [[nodiscard]] static bool IsValid(const ControlledEntityState& value) noexcept
        {
            if (value.controlledEntityGeneration == 0 || !std::isfinite(value.headPositionX) ||
                !std::isfinite(value.headPositionY) || !std::isfinite(value.headingRadians) ||
                !std::isfinite(value.diameter) || value.diameter <= 0.0f || value.bodyTrailSamples.empty())
            {
                return false;
            }
            for (const ControlledEntityBodySample& sample : value.bodyTrailSamples)
            {
                if (!IsValid(sample))
                {
                    return false;
                }
            }
            return true;
        }

        static void WriteBodySample(const ControlledEntityBodySample& sample, const std::size_t offset,
                                    const std::span<std::byte> output) noexcept
        {
            WorldProtocolWireCodec::WriteF32(sample.positionX, offset, output);
            WorldProtocolWireCodec::WriteF32(sample.positionY, offset + sizeof(float), output);
        }

        static void ReadBodySample(const std::size_t offset, const std::span<const std::byte> payload,
                                   ControlledEntityBodySample* const outSample) noexcept
        {
            outSample->positionX = WorldProtocolWireCodec::ReadF32(offset, payload);
            outSample->positionY = WorldProtocolWireCodec::ReadF32(offset + sizeof(float), payload);
        }
    };
} // namespace psnr::world::protocol::v2
