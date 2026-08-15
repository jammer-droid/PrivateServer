#pragma once

#include "WorldProtocolWireCodec.h"

#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace psnr::world::protocol::v1
{
    struct EntitySpawn final
    {
        /*
        0   2      6       10      14    16        20    22      26      30    34    38    42    46    50
        +---+------+-------+-------+-----+---------+-----+-------+-------+-----+-----+-----+-----+-----+
        |ver|tick  |entity |gen    |kind |archetype|shape|radius |maxSpd |posX |posY |velX |velY |angle|
        +---+------+-------+-------+-----+---------+-----+-------+-------+-----+-----+-----+-----+-----+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t EntityIdOffset = 6;
            static constexpr std::size_t GenerationOffset = 10;
            static constexpr std::size_t EntityKindOffset = 14;
            static constexpr std::size_t ArchetypeIdOffset = 16;
            static constexpr std::size_t PrimaryShapeKindOffset = 20;
            static constexpr std::size_t PrimaryCircleRadiusOffset = 22;
            static constexpr std::size_t MaxMoveSpeedOffset = 26;
            static constexpr std::size_t PositionXOffset = 30;
            static constexpr std::size_t PositionYOffset = 34;
            static constexpr std::size_t VelocityXOffset = 38;
            static constexpr std::size_t VelocityYOffset = 42;
            static constexpr std::size_t AngleRadiansOffset = 46;
            static constexpr std::size_t PayloadBytes = 50;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t entityId = 0;
        std::uint32_t generation = 0;
        EntityKind entityKind = EntityKind::Invalid;
        std::uint32_t archetypeId = 0;
        ShapeKind primaryShapeKind = ShapeKind::Invalid;
        float primaryCircleRadius = 0.0f;
        float maxMoveSpeed = 0.0f;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float angleRadians = 0.0f;

        [[nodiscard]] static WorldProtocolError Encode(const EntitySpawn& value, std::span<std::byte> output) noexcept
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
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.entityKind), Wire::EntityKindOffset,
                                             output);
            WorldProtocolWireCodec::WriteU32(value.archetypeId, Wire::ArchetypeIdOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.primaryShapeKind),
                                             Wire::PrimaryShapeKindOffset, output);
            WorldProtocolWireCodec::WriteF32(value.primaryCircleRadius, Wire::PrimaryCircleRadiusOffset, output);
            WorldProtocolWireCodec::WriteF32(value.maxMoveSpeed, Wire::MaxMoveSpeedOffset, output);
            WorldProtocolWireCodec::WriteF32(value.positionX, Wire::PositionXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.positionY, Wire::PositionYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.velocityX, Wire::VelocityXOffset, output);
            WorldProtocolWireCodec::WriteF32(value.velocityY, Wire::VelocityYOffset, output);
            WorldProtocolWireCodec::WriteF32(value.angleRadians, Wire::AngleRadiansOffset, output);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload,
                                                       EntitySpawn* outValue) noexcept
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

            EntitySpawn decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.entityId = WorldProtocolWireCodec::ReadU32(Wire::EntityIdOffset, payload);
            decoded.generation = WorldProtocolWireCodec::ReadU32(Wire::GenerationOffset, payload);
            decoded.entityKind =
                static_cast<EntityKind>(WorldProtocolWireCodec::ReadU16(Wire::EntityKindOffset, payload));
            decoded.archetypeId = WorldProtocolWireCodec::ReadU32(Wire::ArchetypeIdOffset, payload);
            decoded.primaryShapeKind =
                static_cast<ShapeKind>(WorldProtocolWireCodec::ReadU16(Wire::PrimaryShapeKindOffset, payload));
            decoded.primaryCircleRadius = WorldProtocolWireCodec::ReadF32(Wire::PrimaryCircleRadiusOffset, payload);
            decoded.maxMoveSpeed = WorldProtocolWireCodec::ReadF32(Wire::MaxMoveSpeedOffset, payload);
            decoded.positionX = WorldProtocolWireCodec::ReadF32(Wire::PositionXOffset, payload);
            decoded.positionY = WorldProtocolWireCodec::ReadF32(Wire::PositionYOffset, payload);
            decoded.velocityX = WorldProtocolWireCodec::ReadF32(Wire::VelocityXOffset, payload);
            decoded.velocityY = WorldProtocolWireCodec::ReadF32(Wire::VelocityYOffset, payload);
            decoded.angleRadians = WorldProtocolWireCodec::ReadF32(Wire::AngleRadiansOffset, payload);

            const WorldProtocolError validationError = Validate(decoded);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }

            *outValue = decoded;
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend constexpr bool operator==(const EntitySpawn& left,
                                                       const EntitySpawn& right) noexcept = default;

    private:
        [[nodiscard]] static bool IsKnown(const EntityKind value) noexcept
        {
            return value == EntityKind::Player || value == EntityKind::Resource || value == EntityKind::StaticObstacle;
        }

        [[nodiscard]] static bool IsKnown(const ShapeKind value) noexcept
        {
            return value == ShapeKind::Circle;
        }

    public:
        [[nodiscard]] static WorldProtocolError Validate(const EntitySpawn& value) noexcept
        {
            if (!IsKnown(value.entityKind) || !IsKnown(value.primaryShapeKind))
            {
                return WorldProtocolError::InvalidEnum;
            }
            if (value.entityId == 0 || value.generation == 0 || value.archetypeId == 0 ||
                !std::isfinite(value.primaryCircleRadius) || !std::isfinite(value.maxMoveSpeed) ||
                !std::isfinite(value.positionX) || !std::isfinite(value.positionY) || !std::isfinite(value.velocityX) ||
                !std::isfinite(value.velocityY) || !std::isfinite(value.angleRadians) ||
                value.primaryCircleRadius <= 0.0f)
            {
                return WorldProtocolError::InvalidNumeric;
            }
            if (value.entityKind == EntityKind::Player && value.maxMoveSpeed <= 0.0f)
            {
                return WorldProtocolError::InvalidNumeric;
            }
            if (value.entityKind != EntityKind::Player && value.maxMoveSpeed != 0.0f)
            {
                return WorldProtocolError::InvalidNumeric;
            }
            if (value.entityKind == EntityKind::StaticObstacle && (value.velocityX != 0.0f || value.velocityY != 0.0f))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            return WorldProtocolError::Success;
        }
    };
} // namespace psnr::world::protocol::v1

namespace psnr::world::protocol::v2
{
    struct EntitySpawn final
    {
        struct Wire final
        {
            static constexpr std::size_t BaselineBytes = v1::EntitySpawn::Wire::PayloadBytes;
            static constexpr std::size_t PlayerIdOffset = BaselineBytes;
            static constexpr std::size_t DisplayNameByteCountOffset = PlayerIdOffset + 4;
            static constexpr std::size_t DisplayNameOffset = DisplayNameByteCountOffset + 2;
            static constexpr std::size_t MinimumPayloadBytes = DisplayNameOffset;
            static constexpr std::size_t MaximumPayloadBytes =
                DisplayNameOffset + WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes;
        };

        v1::EntitySpawn baseline{};
        std::uint32_t playerId = 0;
        std::string displayName;

        [[nodiscard]] static std::size_t CalculatePayloadBytes(const std::string_view displayName) noexcept
        {
            return displayName.size() <= WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes
                       ? Wire::DisplayNameOffset + displayName.size()
                       : 0;
        }

        [[nodiscard]] static WorldProtocolError Encode(const EntitySpawn& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t payloadBytes = CalculatePayloadBytes(value.displayName);
            if (payloadBytes == 0 || output.size() != payloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            const WorldProtocolError validationError = Validate(value);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }
            if (v1::EntitySpawn::Encode(value.baseline, output.first(Wire::BaselineBytes)) !=
                WorldProtocolError::Success)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, 0, output);
            WorldProtocolWireCodec::WriteU32(value.playerId, Wire::PlayerIdOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.displayName.size()),
                                             Wire::DisplayNameByteCountOffset, output);
            for (std::size_t index = 0; index < value.displayName.size(); ++index)
            {
                output[Wire::DisplayNameOffset + index] = static_cast<std::byte>(value.displayName[index]);
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       EntitySpawn* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::MinimumPayloadBytes || payload.size() > Wire::MaximumPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(0, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }
            const std::uint16_t displayNameByteCount =
                WorldProtocolWireCodec::ReadU16(Wire::DisplayNameByteCountOffset, payload);
            if (payload.size() != Wire::DisplayNameOffset + displayNameByteCount)
            {
                return WorldProtocolError::InvalidLength;
            }

            std::array<std::byte, v1::EntitySpawn::Wire::PayloadBytes> baselineBytes{};
            for (std::size_t index = 0; index < baselineBytes.size(); ++index)
            {
                baselineBytes[index] = payload[index];
            }
            WorldProtocolWireCodec::WriteU16(v1::PayloadVersion, 0, baselineBytes);

            EntitySpawn decoded;
            const WorldProtocolError baselineError = v1::EntitySpawn::Decode(baselineBytes, &decoded.baseline);
            if (baselineError != WorldProtocolError::Success)
            {
                return baselineError;
            }
            decoded.playerId = WorldProtocolWireCodec::ReadU32(Wire::PlayerIdOffset, payload);
            const std::span<const std::byte> displayNameBytes = payload.subspan(Wire::DisplayNameOffset);
            if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(displayNameBytes))
            {
                return WorldProtocolError::InvalidArgument;
            }
            decoded.displayName.reserve(displayNameBytes.size());
            for (const std::byte character : displayNameBytes)
            {
                decoded.displayName.push_back(static_cast<char>(std::to_integer<std::uint8_t>(character)));
            }
            const WorldProtocolError validationError = Validate(decoded);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }
            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Validate(const EntitySpawn& value) noexcept
        {
            const WorldProtocolError baselineError = v1::EntitySpawn::Validate(value.baseline);
            if (baselineError != WorldProtocolError::Success)
            {
                return baselineError;
            }
            if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(value.displayName))
            {
                return WorldProtocolError::InvalidArgument;
            }
            const bool isPlayer = value.baseline.entityKind == EntityKind::Player;
            if ((isPlayer && value.playerId == 0) || (!isPlayer && (value.playerId != 0 || !value.displayName.empty())))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const EntitySpawn&, const EntitySpawn&) noexcept = default;
    };
} // namespace psnr::world::protocol::v2
