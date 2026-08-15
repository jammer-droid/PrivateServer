#pragma once

#include "WorldProtocolWireCodec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace psnr::world::protocol::v2
{
    struct WorldOverviewPoint final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        [[nodiscard]] friend constexpr bool operator==(const WorldOverviewPoint&, const WorldOverviewPoint&) noexcept =
            default;
    };

    struct WorldOverviewPlayer final
    {
        std::uint32_t playerId = 0;
        std::uint32_t growthPoint = 0;
        std::vector<WorldOverviewPoint> bodySamples;
        [[nodiscard]] friend bool operator==(const WorldOverviewPlayer&, const WorldOverviewPlayer&) noexcept = default;
    };

    struct WorldOverviewLeaderboardEntry final
    {
        std::uint16_t rank = 0;
        std::uint32_t playerId = 0;
        std::uint32_t growthPoint = 0;
        [[nodiscard]] friend constexpr bool operator==(const WorldOverviewLeaderboardEntry&,
                                                       const WorldOverviewLeaderboardEntry&) noexcept = default;
    };

    struct WorldOverviewSnapshot final
    {
        struct Wire final
        {
            static constexpr std::size_t HeaderBytes = 46;
            static constexpr std::size_t PlayerHeaderBytes = 10;
            static constexpr std::size_t PointBytes = 8;
            static constexpr std::size_t LeaderboardEntryBytes = 10;
            static constexpr std::size_t MaximumPayloadBytes = 8186;
            static constexpr std::size_t MaximumLeaderboardEntryCount = 10;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t overviewId = 0;
        std::uint16_t chunkIndex = 0;
        std::uint16_t chunkCount = 0;
        float mapMinX = 0.0f;
        float mapMinY = 0.0f;
        float mapMaxX = 0.0f;
        float mapMaxY = 0.0f;
        float activeAreaCenterX = 0.0f;
        float activeAreaCenterY = 0.0f;
        float activeAreaRadius = 0.0f;
        std::vector<WorldOverviewPlayer> players;
        std::vector<WorldOverviewLeaderboardEntry> leaderboard;

        [[nodiscard]] static std::size_t CalculatePayloadBytes(const WorldOverviewSnapshot& value) noexcept
        {
            if (value.players.size() > std::numeric_limits<std::uint16_t>::max() ||
                value.leaderboard.size() > Wire::MaximumLeaderboardEntryCount)
            {
                return 0;
            }
            std::size_t size = Wire::HeaderBytes;
            for (const WorldOverviewPlayer& player : value.players)
            {
                if (player.bodySamples.empty() ||
                    player.bodySamples.size() > std::numeric_limits<std::uint16_t>::max())
                {
                    return 0;
                }
                const std::size_t recordBytes =
                    Wire::PlayerHeaderBytes + player.bodySamples.size() * Wire::PointBytes;
                if (recordBytes > Wire::MaximumPayloadBytes - size)
                {
                    return 0;
                }
                size += recordBytes;
            }
            const std::size_t leaderboardBytes = value.leaderboard.size() * Wire::LeaderboardEntryBytes;
            return leaderboardBytes <= Wire::MaximumPayloadBytes - size ? size + leaderboardBytes : 0;
        }

        [[nodiscard]] static WorldProtocolError Encode(const WorldOverviewSnapshot& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t payloadBytes = CalculatePayloadBytes(value);
            if (payloadBytes == 0 || output.size() != payloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!IsValid(value))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, 0, output);
            WorldProtocolWireCodec::WriteU32(value.serverTick, 2, output);
            WorldProtocolWireCodec::WriteU32(value.overviewId, 6, output);
            WorldProtocolWireCodec::WriteU16(value.chunkIndex, 10, output);
            WorldProtocolWireCodec::WriteU16(value.chunkCount, 12, output);
            WorldProtocolWireCodec::WriteF32(value.mapMinX, 14, output);
            WorldProtocolWireCodec::WriteF32(value.mapMinY, 18, output);
            WorldProtocolWireCodec::WriteF32(value.mapMaxX, 22, output);
            WorldProtocolWireCodec::WriteF32(value.mapMaxY, 26, output);
            WorldProtocolWireCodec::WriteF32(value.activeAreaCenterX, 30, output);
            WorldProtocolWireCodec::WriteF32(value.activeAreaCenterY, 34, output);
            WorldProtocolWireCodec::WriteF32(value.activeAreaRadius, 38, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.players.size()), 42, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.leaderboard.size()), 44, output);

            std::size_t offset = Wire::HeaderBytes;
            for (const WorldOverviewPlayer& player : value.players)
            {
                WorldProtocolWireCodec::WriteU32(player.playerId, offset, output);
                WorldProtocolWireCodec::WriteU32(player.growthPoint, offset + 4, output);
                WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(player.bodySamples.size()), offset + 8,
                                                 output);
                offset += Wire::PlayerHeaderBytes;
                for (const WorldOverviewPoint& point : player.bodySamples)
                {
                    WorldProtocolWireCodec::WriteF32(point.positionX, offset, output);
                    WorldProtocolWireCodec::WriteF32(point.positionY, offset + 4, output);
                    offset += Wire::PointBytes;
                }
            }
            for (const WorldOverviewLeaderboardEntry& entry : value.leaderboard)
            {
                WorldProtocolWireCodec::WriteU16(entry.rank, offset, output);
                WorldProtocolWireCodec::WriteU32(entry.playerId, offset + 2, output);
                WorldProtocolWireCodec::WriteU32(entry.growthPoint, offset + 6, output);
                offset += Wire::LeaderboardEntryBytes;
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       WorldOverviewSnapshot* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::HeaderBytes || payload.size() > Wire::MaximumPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(0, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }

            WorldOverviewSnapshot decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(2, payload);
            decoded.overviewId = WorldProtocolWireCodec::ReadU32(6, payload);
            decoded.chunkIndex = WorldProtocolWireCodec::ReadU16(10, payload);
            decoded.chunkCount = WorldProtocolWireCodec::ReadU16(12, payload);
            decoded.mapMinX = WorldProtocolWireCodec::ReadF32(14, payload);
            decoded.mapMinY = WorldProtocolWireCodec::ReadF32(18, payload);
            decoded.mapMaxX = WorldProtocolWireCodec::ReadF32(22, payload);
            decoded.mapMaxY = WorldProtocolWireCodec::ReadF32(26, payload);
            decoded.activeAreaCenterX = WorldProtocolWireCodec::ReadF32(30, payload);
            decoded.activeAreaCenterY = WorldProtocolWireCodec::ReadF32(34, payload);
            decoded.activeAreaRadius = WorldProtocolWireCodec::ReadF32(38, payload);
            const std::uint16_t playerCount = WorldProtocolWireCodec::ReadU16(42, payload);
            const std::uint16_t leaderboardCount = WorldProtocolWireCodec::ReadU16(44, payload);
            constexpr std::size_t MinimumPlayerBytes = Wire::PlayerHeaderBytes + Wire::PointBytes;
            if (playerCount > (payload.size() - Wire::HeaderBytes) / MinimumPlayerBytes ||
                leaderboardCount > Wire::MaximumLeaderboardEntryCount)
            {
                return WorldProtocolError::InvalidLength;
            }

            std::size_t offset = Wire::HeaderBytes;
            decoded.players.reserve(playerCount);
            for (std::uint16_t index = 0; index < playerCount; ++index)
            {
                if (payload.size() - offset < Wire::PlayerHeaderBytes)
                {
                    return WorldProtocolError::InvalidLength;
                }
                WorldOverviewPlayer player;
                player.playerId = WorldProtocolWireCodec::ReadU32(offset, payload);
                player.growthPoint = WorldProtocolWireCodec::ReadU32(offset + 4, payload);
                const std::uint16_t sampleCount = WorldProtocolWireCodec::ReadU16(offset + 8, payload);
                offset += Wire::PlayerHeaderBytes;
                const std::size_t sampleBytes = static_cast<std::size_t>(sampleCount) * Wire::PointBytes;
                if (sampleCount == 0 || sampleBytes > payload.size() - offset)
                {
                    return WorldProtocolError::InvalidLength;
                }
                player.bodySamples.resize(sampleCount);
                for (WorldOverviewPoint& point : player.bodySamples)
                {
                    point.positionX = WorldProtocolWireCodec::ReadF32(offset, payload);
                    point.positionY = WorldProtocolWireCodec::ReadF32(offset + 4, payload);
                    offset += Wire::PointBytes;
                }
                decoded.players.push_back(std::move(player));
            }
            const std::size_t expectedLeaderboardBytes =
                static_cast<std::size_t>(leaderboardCount) * Wire::LeaderboardEntryBytes;
            if (expectedLeaderboardBytes != payload.size() - offset)
            {
                return WorldProtocolError::InvalidLength;
            }
            decoded.leaderboard.resize(leaderboardCount);
            for (WorldOverviewLeaderboardEntry& entry : decoded.leaderboard)
            {
                entry.rank = WorldProtocolWireCodec::ReadU16(offset, payload);
                entry.playerId = WorldProtocolWireCodec::ReadU32(offset + 2, payload);
                entry.growthPoint = WorldProtocolWireCodec::ReadU32(offset + 6, payload);
                offset += Wire::LeaderboardEntryBytes;
            }
            if (!IsValid(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const WorldOverviewSnapshot&, const WorldOverviewSnapshot&) noexcept =
            default;

    private:
        [[nodiscard]] static bool IsValid(const WorldOverviewSnapshot& value) noexcept
        {
            if (value.overviewId == 0 || value.chunkCount == 0 || value.chunkIndex >= value.chunkCount ||
                (value.chunkIndex != 0 && !value.leaderboard.empty()) || !std::isfinite(value.mapMinX) ||
                !std::isfinite(value.mapMinY) || !std::isfinite(value.mapMaxX) || !std::isfinite(value.mapMaxY) ||
                value.mapMinX >= value.mapMaxX || value.mapMinY >= value.mapMaxY ||
                !std::isfinite(value.activeAreaCenterX) || !std::isfinite(value.activeAreaCenterY) ||
                !std::isfinite(value.activeAreaRadius) || value.activeAreaRadius <= 0.0f)
            {
                return false;
            }
            for (const WorldOverviewPlayer& player : value.players)
            {
                if (player.playerId == 0 || player.bodySamples.empty())
                {
                    return false;
                }
                for (const WorldOverviewPoint& point : player.bodySamples)
                {
                    if (!std::isfinite(point.positionX) || !std::isfinite(point.positionY))
                    {
                        return false;
                    }
                }
            }
            for (const WorldOverviewLeaderboardEntry& entry : value.leaderboard)
            {
                if (entry.rank == 0 || entry.playerId == 0)
                {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace psnr::world::protocol::v2

namespace psnr::world::protocol::v3
{
    using WorldOverviewPoint = v2::WorldOverviewPoint;
    using WorldOverviewPlayer = v2::WorldOverviewPlayer;

    struct WorldOverviewLeaderboardEntry final
    {
        std::uint16_t rank = 0;
        std::uint32_t playerId = 0;
        std::uint32_t growthPoint = 0;
        std::string displayName;

        [[nodiscard]] friend bool operator==(const WorldOverviewLeaderboardEntry&,
                                             const WorldOverviewLeaderboardEntry&) noexcept = default;
    };

    struct WorldOverviewSnapshot final
    {
        struct Wire final
        {
            static constexpr std::size_t HeaderBytes = 46;
            static constexpr std::size_t PlayerHeaderBytes = 10;
            static constexpr std::size_t PointBytes = 8;
            static constexpr std::size_t LeaderboardEntryHeaderBytes = 12;
            static constexpr std::size_t MaximumPayloadBytes = 8186;
            static constexpr std::size_t MaximumLeaderboardEntryCount = 10;
        };

        std::uint32_t serverTick = 0;
        std::uint32_t overviewId = 0;
        std::uint16_t chunkIndex = 0;
        std::uint16_t chunkCount = 0;
        float mapMinX = 0.0f;
        float mapMinY = 0.0f;
        float mapMaxX = 0.0f;
        float mapMaxY = 0.0f;
        float activeAreaCenterX = 0.0f;
        float activeAreaCenterY = 0.0f;
        float activeAreaRadius = 0.0f;
        std::vector<WorldOverviewPlayer> players;
        std::vector<WorldOverviewLeaderboardEntry> leaderboard;

        [[nodiscard]] static std::size_t CalculatePayloadBytes(const WorldOverviewSnapshot& value) noexcept
        {
            if (value.players.size() > std::numeric_limits<std::uint16_t>::max() ||
                value.leaderboard.size() > Wire::MaximumLeaderboardEntryCount)
            {
                return 0;
            }
            std::size_t size = Wire::HeaderBytes;
            for (const WorldOverviewPlayer& player : value.players)
            {
                if (player.bodySamples.empty() ||
                    player.bodySamples.size() > std::numeric_limits<std::uint16_t>::max())
                {
                    return 0;
                }
                const std::size_t recordBytes =
                    Wire::PlayerHeaderBytes + player.bodySamples.size() * Wire::PointBytes;
                if (recordBytes > Wire::MaximumPayloadBytes - size)
                {
                    return 0;
                }
                size += recordBytes;
            }
            for (const WorldOverviewLeaderboardEntry& entry : value.leaderboard)
            {
                if (entry.displayName.size() > WorldProtocolWireCodec::MaximumPlayerDisplayNameBytes)
                {
                    return 0;
                }
                const std::size_t recordBytes = Wire::LeaderboardEntryHeaderBytes + entry.displayName.size();
                if (recordBytes > Wire::MaximumPayloadBytes - size)
                {
                    return 0;
                }
                size += recordBytes;
            }
            return size;
        }

        [[nodiscard]] static WorldProtocolError Encode(const WorldOverviewSnapshot& value,
                                                       const std::span<std::byte> output) noexcept
        {
            const std::size_t payloadBytes = CalculatePayloadBytes(value);
            if (payloadBytes == 0 || output.size() != payloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!IsValidNumeric(value))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            for (const WorldOverviewLeaderboardEntry& entry : value.leaderboard)
            {
                if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(entry.displayName))
                {
                    return WorldProtocolError::InvalidArgument;
                }
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, 0, output);
            WorldProtocolWireCodec::WriteU32(value.serverTick, 2, output);
            WorldProtocolWireCodec::WriteU32(value.overviewId, 6, output);
            WorldProtocolWireCodec::WriteU16(value.chunkIndex, 10, output);
            WorldProtocolWireCodec::WriteU16(value.chunkCount, 12, output);
            WorldProtocolWireCodec::WriteF32(value.mapMinX, 14, output);
            WorldProtocolWireCodec::WriteF32(value.mapMinY, 18, output);
            WorldProtocolWireCodec::WriteF32(value.mapMaxX, 22, output);
            WorldProtocolWireCodec::WriteF32(value.mapMaxY, 26, output);
            WorldProtocolWireCodec::WriteF32(value.activeAreaCenterX, 30, output);
            WorldProtocolWireCodec::WriteF32(value.activeAreaCenterY, 34, output);
            WorldProtocolWireCodec::WriteF32(value.activeAreaRadius, 38, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.players.size()), 42, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.leaderboard.size()), 44, output);

            std::size_t offset = Wire::HeaderBytes;
            for (const WorldOverviewPlayer& player : value.players)
            {
                WorldProtocolWireCodec::WriteU32(player.playerId, offset, output);
                WorldProtocolWireCodec::WriteU32(player.growthPoint, offset + 4, output);
                WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(player.bodySamples.size()), offset + 8,
                                                 output);
                offset += Wire::PlayerHeaderBytes;
                for (const WorldOverviewPoint& point : player.bodySamples)
                {
                    WorldProtocolWireCodec::WriteF32(point.positionX, offset, output);
                    WorldProtocolWireCodec::WriteF32(point.positionY, offset + 4, output);
                    offset += Wire::PointBytes;
                }
            }
            for (const WorldOverviewLeaderboardEntry& entry : value.leaderboard)
            {
                WorldProtocolWireCodec::WriteU16(entry.rank, offset, output);
                WorldProtocolWireCodec::WriteU32(entry.playerId, offset + 2, output);
                WorldProtocolWireCodec::WriteU32(entry.growthPoint, offset + 6, output);
                WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(entry.displayName.size()), offset + 10,
                                                 output);
                offset += Wire::LeaderboardEntryHeaderBytes;
                for (const char character : entry.displayName)
                {
                    output[offset] = static_cast<std::byte>(character);
                    ++offset;
                }
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       WorldOverviewSnapshot* const outValue)
        {
            if (outValue == nullptr)
            {
                return WorldProtocolError::InvalidArgument;
            }
            if (payload.size() < Wire::HeaderBytes || payload.size() > Wire::MaximumPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (WorldProtocolWireCodec::ReadU16(0, payload) != PayloadVersion)
            {
                return WorldProtocolError::UnsupportedVersion;
            }

            WorldOverviewSnapshot decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(2, payload);
            decoded.overviewId = WorldProtocolWireCodec::ReadU32(6, payload);
            decoded.chunkIndex = WorldProtocolWireCodec::ReadU16(10, payload);
            decoded.chunkCount = WorldProtocolWireCodec::ReadU16(12, payload);
            decoded.mapMinX = WorldProtocolWireCodec::ReadF32(14, payload);
            decoded.mapMinY = WorldProtocolWireCodec::ReadF32(18, payload);
            decoded.mapMaxX = WorldProtocolWireCodec::ReadF32(22, payload);
            decoded.mapMaxY = WorldProtocolWireCodec::ReadF32(26, payload);
            decoded.activeAreaCenterX = WorldProtocolWireCodec::ReadF32(30, payload);
            decoded.activeAreaCenterY = WorldProtocolWireCodec::ReadF32(34, payload);
            decoded.activeAreaRadius = WorldProtocolWireCodec::ReadF32(38, payload);
            const std::uint16_t playerCount = WorldProtocolWireCodec::ReadU16(42, payload);
            const std::uint16_t leaderboardCount = WorldProtocolWireCodec::ReadU16(44, payload);
            constexpr std::size_t MinimumPlayerBytes = Wire::PlayerHeaderBytes + Wire::PointBytes;
            if (playerCount > (payload.size() - Wire::HeaderBytes) / MinimumPlayerBytes ||
                leaderboardCount > Wire::MaximumLeaderboardEntryCount)
            {
                return WorldProtocolError::InvalidLength;
            }

            std::size_t offset = Wire::HeaderBytes;
            decoded.players.reserve(playerCount);
            for (std::uint16_t index = 0; index < playerCount; ++index)
            {
                if (payload.size() - offset < Wire::PlayerHeaderBytes)
                {
                    return WorldProtocolError::InvalidLength;
                }
                WorldOverviewPlayer player;
                player.playerId = WorldProtocolWireCodec::ReadU32(offset, payload);
                player.growthPoint = WorldProtocolWireCodec::ReadU32(offset + 4, payload);
                const std::uint16_t sampleCount = WorldProtocolWireCodec::ReadU16(offset + 8, payload);
                offset += Wire::PlayerHeaderBytes;
                const std::size_t sampleBytes = static_cast<std::size_t>(sampleCount) * Wire::PointBytes;
                if (sampleCount == 0 || sampleBytes > payload.size() - offset)
                {
                    return WorldProtocolError::InvalidLength;
                }
                player.bodySamples.resize(sampleCount);
                for (WorldOverviewPoint& point : player.bodySamples)
                {
                    point.positionX = WorldProtocolWireCodec::ReadF32(offset, payload);
                    point.positionY = WorldProtocolWireCodec::ReadF32(offset + 4, payload);
                    offset += Wire::PointBytes;
                }
                decoded.players.push_back(std::move(player));
            }

            decoded.leaderboard.reserve(leaderboardCount);
            for (std::uint16_t index = 0; index < leaderboardCount; ++index)
            {
                if (payload.size() - offset < Wire::LeaderboardEntryHeaderBytes)
                {
                    return WorldProtocolError::InvalidLength;
                }
                WorldOverviewLeaderboardEntry entry;
                entry.rank = WorldProtocolWireCodec::ReadU16(offset, payload);
                entry.playerId = WorldProtocolWireCodec::ReadU32(offset + 2, payload);
                entry.growthPoint = WorldProtocolWireCodec::ReadU32(offset + 6, payload);
                const std::uint16_t displayNameByteCount = WorldProtocolWireCodec::ReadU16(offset + 10, payload);
                offset += Wire::LeaderboardEntryHeaderBytes;
                if (displayNameByteCount > payload.size() - offset)
                {
                    return WorldProtocolError::InvalidLength;
                }
                const std::span<const std::byte> displayNameBytes = payload.subspan(offset, displayNameByteCount);
                if (!WorldProtocolWireCodec::IsValidPlayerDisplayName(displayNameBytes))
                {
                    return WorldProtocolError::InvalidArgument;
                }
                entry.displayName.reserve(displayNameBytes.size());
                for (const std::byte character : displayNameBytes)
                {
                    entry.displayName.push_back(static_cast<char>(std::to_integer<std::uint8_t>(character)));
                }
                offset += displayNameBytes.size();
                decoded.leaderboard.push_back(std::move(entry));
            }
            if (offset != payload.size())
            {
                return WorldProtocolError::InvalidLength;
            }
            if (!IsValidNumeric(decoded))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const WorldOverviewSnapshot&, const WorldOverviewSnapshot&) noexcept =
            default;

    private:
        [[nodiscard]] static bool IsValidNumeric(const WorldOverviewSnapshot& value) noexcept
        {
            if (value.overviewId == 0 || value.chunkCount == 0 || value.chunkIndex >= value.chunkCount ||
                (value.chunkIndex != 0 && !value.leaderboard.empty()) || !std::isfinite(value.mapMinX) ||
                !std::isfinite(value.mapMinY) || !std::isfinite(value.mapMaxX) || !std::isfinite(value.mapMaxY) ||
                value.mapMinX >= value.mapMaxX || value.mapMinY >= value.mapMaxY ||
                !std::isfinite(value.activeAreaCenterX) || !std::isfinite(value.activeAreaCenterY) ||
                !std::isfinite(value.activeAreaRadius) || value.activeAreaRadius <= 0.0f)
            {
                return false;
            }
            for (const WorldOverviewPlayer& player : value.players)
            {
                if (player.playerId == 0 || player.bodySamples.empty())
                {
                    return false;
                }
                for (const WorldOverviewPoint& point : player.bodySamples)
                {
                    if (!std::isfinite(point.positionX) || !std::isfinite(point.positionY))
                    {
                        return false;
                    }
                }
            }
            for (const WorldOverviewLeaderboardEntry& entry : value.leaderboard)
            {
                if (entry.rank == 0 || entry.playerId == 0)
                {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace psnr::world::protocol::v3
