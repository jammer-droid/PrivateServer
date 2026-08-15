#pragma once

#include "WorldProtocolWireCodec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace psnr::world::protocol::v1
{
    struct EntityStateRecord final
    {
        /*
        0        4          8        12       16       20       24       28
        +--------+----------+--------+--------+--------+--------+--------+
        |entityId|generation|posX    |posY    |velX    |velY    |angle   |
        +--------+----------+--------+--------+--------+--------+--------+
        */
        struct Wire final
        {
            static constexpr std::size_t EntityIdOffset = 0;
            static constexpr std::size_t GenerationOffset = 4;
            static constexpr std::size_t PositionXOffset = 8;
            static constexpr std::size_t PositionYOffset = 12;
            static constexpr std::size_t VelocityXOffset = 16;
            static constexpr std::size_t VelocityYOffset = 20;
            static constexpr std::size_t AngleRadiansOffset = 24;
            static constexpr std::size_t RecordBytes = 28;
        };

        std::uint32_t entityId = 0;
        std::uint32_t generation = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float angleRadians = 0.0f;

        [[nodiscard]] friend constexpr bool operator==(const EntityStateRecord& left,
                                                       const EntityStateRecord& right) noexcept = default;
    };

    struct EntityStateBatch final
    {
        /*
        Header
        0         2         4             8
        +---------+---------+-------------+
        | version | count   | serverTick  |
        +---------+---------+-------------+

        Records
        8 + index * EntityStateRecord::Wire::RecordBytes
        +---------------------------------------------------------------+
        | EntityStateRecord[0] | EntityStateRecord[1] | ...              |
        +---------------------------------------------------------------+
        */
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t RecordCountOffset = 2;
            static constexpr std::size_t ServerTickOffset = 4;
            static constexpr std::size_t HeaderBytes = 8;
            static constexpr std::size_t MaxRecords = 292;

            [[nodiscard]] static constexpr std::size_t CalculatePayloadBytes(const std::size_t recordCount) noexcept
            {
                return recordCount <= MaxRecords ? HeaderBytes + recordCount * EntityStateRecord::Wire::RecordBytes : 0;
            }
        };

        std::uint32_t serverTick = 0;
        std::vector<EntityStateRecord> records;

        [[nodiscard]] static WorldProtocolError Validate(const std::span<const EntityStateRecord> records) noexcept
        {
            if (records.size() > Wire::MaxRecords)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (records.empty() || !AreValid(records))
            {
                return WorldProtocolError::InvalidNumeric;
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Encode(const EntityStateBatch& value,
                                                       std::span<std::byte> output) noexcept
        {
            return Encode(value.serverTick, value.records, output);
        }

        [[nodiscard]] static WorldProtocolError Encode(const std::uint32_t serverTick,
                                                       const std::span<const EntityStateRecord> records,
                                                       std::span<std::byte> output) noexcept
        {
            const std::size_t recordCount = records.size();
            if (recordCount > Wire::MaxRecords)
            {
                return WorldProtocolError::InvalidLength;
            }

            const std::size_t expectedPayloadBytes = Wire::CalculatePayloadBytes(recordCount);
            if (output.size() != expectedPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            const WorldProtocolError validationError = Validate(records);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }

            WorldProtocolWireCodec::WriteU16(PayloadVersion, Wire::PayloadVersionOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(recordCount), Wire::RecordCountOffset, output);
            WorldProtocolWireCodec::WriteU32(serverTick, Wire::ServerTickOffset, output);

            for (std::size_t index = 0; index < recordCount; ++index)
            {
                WriteRecord(records[index], RecordOffset(index), output);
            }

            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(std::span<const std::byte> payload, EntityStateBatch* outValue)
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

            const std::uint16_t recordCount = WorldProtocolWireCodec::ReadU16(Wire::RecordCountOffset, payload);
            if (recordCount > Wire::MaxRecords)
            {
                return WorldProtocolError::InvalidLength;
            }

            const std::size_t expectedPayloadBytes = Wire::CalculatePayloadBytes(recordCount);
            if (payload.size() != expectedPayloadBytes)
            {
                return WorldProtocolError::InvalidLength;
            }
            if (recordCount == 0)
            {
                return WorldProtocolError::InvalidNumeric;
            }

            EntityStateBatch decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.records.resize(recordCount);
            for (std::size_t index = 0; index < recordCount; ++index)
            {
                ReadRecord(RecordOffset(index), payload, &decoded.records[index]);
            }
            if (!AreValid(decoded.records))
            {
                return WorldProtocolError::InvalidNumeric;
            }

            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const EntityStateBatch& left,
                                             const EntityStateBatch& right) noexcept = default;

    private:
        [[nodiscard]] static constexpr std::size_t RecordOffset(const std::size_t index) noexcept
        {
            return Wire::HeaderBytes + index * EntityStateRecord::Wire::RecordBytes;
        }

        [[nodiscard]] static bool IsValid(const EntityStateRecord& value) noexcept
        {
            return value.entityId != 0 && value.generation != 0 && std::isfinite(value.positionX) &&
                   std::isfinite(value.positionY) && std::isfinite(value.velocityX) && std::isfinite(value.velocityY) &&
                   std::isfinite(value.angleRadians);
        }

        [[nodiscard]] static bool AreValid(const std::span<const EntityStateRecord> records) noexcept
        {
            for (std::size_t index = 0; index < records.size(); ++index)
            {
                if (!IsValid(records[index]))
                {
                    return false;
                }
            }

            return true;
        }

        static void WriteRecord(const EntityStateRecord& record, const std::size_t offset,
                                const std::span<std::byte> output) noexcept
        {
            WorldProtocolWireCodec::WriteU32(record.entityId, offset + EntityStateRecord::Wire::EntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(record.generation, offset + EntityStateRecord::Wire::GenerationOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(record.positionX, offset + EntityStateRecord::Wire::PositionXOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(record.positionY, offset + EntityStateRecord::Wire::PositionYOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(record.velocityX, offset + EntityStateRecord::Wire::VelocityXOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(record.velocityY, offset + EntityStateRecord::Wire::VelocityYOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(record.angleRadians, offset + EntityStateRecord::Wire::AngleRadiansOffset,
                                             output);
        }

        static void ReadRecord(const std::size_t offset, const std::span<const std::byte> payload,
                               EntityStateRecord* const outRecord) noexcept
        {
            outRecord->entityId =
                WorldProtocolWireCodec::ReadU32(offset + EntityStateRecord::Wire::EntityIdOffset, payload);
            outRecord->generation =
                WorldProtocolWireCodec::ReadU32(offset + EntityStateRecord::Wire::GenerationOffset, payload);
            outRecord->positionX =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::PositionXOffset, payload);
            outRecord->positionY =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::PositionYOffset, payload);
            outRecord->velocityX =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::VelocityXOffset, payload);
            outRecord->velocityY =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::VelocityYOffset, payload);
            outRecord->angleRadians =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::AngleRadiansOffset, payload);
        }
    };
} // namespace psnr::world::protocol::v1

namespace psnr::world::protocol::v2
{
    struct EntityStateBodySample final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;

        [[nodiscard]] friend constexpr bool operator==(const EntityStateBodySample& left,
                                                       const EntityStateBodySample& right) noexcept = default;
    };

    struct EntityStateRecord final
    {
        struct Wire final
        {
            static constexpr std::size_t EntityIdOffset = 0;
            static constexpr std::size_t GenerationOffset = 4;
            static constexpr std::size_t HeadPositionXOffset = 8;
            static constexpr std::size_t HeadPositionYOffset = 12;
            static constexpr std::size_t HeadingRadiansOffset = 16;
            static constexpr std::size_t DiameterOffset = 20;
            static constexpr std::size_t GrowthPointOffset = 24;
            static constexpr std::size_t BoostStateOffset = 28;
            static constexpr std::size_t BodySampleCountOffset = 30;
            static constexpr std::size_t HeaderBytes = 32;
            static constexpr std::size_t BodySampleBytes = 8;
        };

        std::uint32_t entityId = 0;
        std::uint32_t generation = 0;
        float headPositionX = 0.0f;
        float headPositionY = 0.0f;
        float headingRadians = 0.0f;
        float diameter = 0.0f;
        std::uint32_t growthPoint = 0;
        BoostState boostState = BoostState::Invalid;
        std::vector<EntityStateBodySample> bodyTrailSamples;

        [[nodiscard]] friend bool operator==(const EntityStateRecord& left,
                                             const EntityStateRecord& right) noexcept = default;
    };

    struct EntityStateBatch final
    {
        struct Wire final
        {
            static constexpr std::size_t PayloadVersionOffset = 0;
            static constexpr std::size_t ServerTickOffset = 2;
            static constexpr std::size_t SnapshotIdOffset = 6;
            static constexpr std::size_t ChunkIndexOffset = 10;
            static constexpr std::size_t ChunkCountOffset = 12;
            static constexpr std::size_t RecordCountOffset = 14;
            static constexpr std::size_t HeaderBytes = 16;
            static constexpr std::size_t MaximumPayloadBytes = 8186;

            [[nodiscard]] static std::size_t CalculatePayloadBytes(
                const std::span<const EntityStateRecord> records) noexcept
            {
                std::size_t payloadBytes = HeaderBytes;
                for (const EntityStateRecord& record : records)
                {
                    if (record.bodyTrailSamples.size() > std::numeric_limits<std::uint16_t>::max())
                    {
                        return 0;
                    }
                    const std::size_t remainingBytes = MaximumPayloadBytes - payloadBytes;
                    const std::size_t maximumSamples = remainingBytes >= EntityStateRecord::Wire::HeaderBytes
                                                           ? (remainingBytes - EntityStateRecord::Wire::HeaderBytes) /
                                                                 EntityStateRecord::Wire::BodySampleBytes
                                                           : 0;
                    if (record.bodyTrailSamples.size() > maximumSamples)
                    {
                        return 0;
                    }
                    payloadBytes += EntityStateRecord::Wire::HeaderBytes +
                                    record.bodyTrailSamples.size() * EntityStateRecord::Wire::BodySampleBytes;
                }
                return payloadBytes;
            }
        };

        std::uint32_t serverTick = 0;
        std::uint32_t snapshotId = 0;
        std::uint16_t chunkIndex = 0;
        std::uint16_t chunkCount = 0;
        std::vector<EntityStateRecord> records;

        [[nodiscard]] static WorldProtocolError Encode(const EntityStateBatch& value,
                                                       const std::span<std::byte> output) noexcept
        {
            if (value.records.empty() || value.records.size() > std::numeric_limits<std::uint16_t>::max())
            {
                return WorldProtocolError::InvalidLength;
            }
            const std::size_t payloadBytes = Wire::CalculatePayloadBytes(value.records);
            if (payloadBytes == 0 || output.size() != payloadBytes)
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
            WorldProtocolWireCodec::WriteU32(value.snapshotId, Wire::SnapshotIdOffset, output);
            WorldProtocolWireCodec::WriteU16(value.chunkIndex, Wire::ChunkIndexOffset, output);
            WorldProtocolWireCodec::WriteU16(value.chunkCount, Wire::ChunkCountOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(value.records.size()), Wire::RecordCountOffset,
                                             output);
            std::size_t offset = Wire::HeaderBytes;
            for (const EntityStateRecord& record : value.records)
            {
                WriteRecord(record, offset, output);
                offset += EntityStateRecord::Wire::HeaderBytes +
                          record.bodyTrailSamples.size() * EntityStateRecord::Wire::BodySampleBytes;
            }
            return WorldProtocolError::Success;
        }

        [[nodiscard]] static WorldProtocolError Decode(const std::span<const std::byte> payload,
                                                       EntityStateBatch* const outValue)
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

            const std::uint16_t recordCount = WorldProtocolWireCodec::ReadU16(Wire::RecordCountOffset, payload);
            const std::size_t minimumRecordBytes =
                EntityStateRecord::Wire::HeaderBytes + EntityStateRecord::Wire::BodySampleBytes;
            if (recordCount == 0 || recordCount > (payload.size() - Wire::HeaderBytes) / minimumRecordBytes)
            {
                return WorldProtocolError::InvalidLength;
            }

            EntityStateBatch decoded;
            decoded.serverTick = WorldProtocolWireCodec::ReadU32(Wire::ServerTickOffset, payload);
            decoded.snapshotId = WorldProtocolWireCodec::ReadU32(Wire::SnapshotIdOffset, payload);
            decoded.chunkIndex = WorldProtocolWireCodec::ReadU16(Wire::ChunkIndexOffset, payload);
            decoded.chunkCount = WorldProtocolWireCodec::ReadU16(Wire::ChunkCountOffset, payload);
            decoded.records.reserve(recordCount);
            std::size_t offset = Wire::HeaderBytes;
            for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
            {
                if (payload.size() - offset < EntityStateRecord::Wire::HeaderBytes)
                {
                    return WorldProtocolError::InvalidLength;
                }
                const std::uint16_t bodySampleCount =
                    WorldProtocolWireCodec::ReadU16(offset + EntityStateRecord::Wire::BodySampleCountOffset, payload);
                const std::size_t remainingBytes = payload.size() - offset - EntityStateRecord::Wire::HeaderBytes;
                if (bodySampleCount == 0 || bodySampleCount > remainingBytes / EntityStateRecord::Wire::BodySampleBytes)
                {
                    return WorldProtocolError::InvalidLength;
                }
                EntityStateRecord record;
                ReadRecord(offset, bodySampleCount, payload, &record);
                decoded.records.push_back(std::move(record));
                offset += EntityStateRecord::Wire::HeaderBytes +
                          static_cast<std::size_t>(bodySampleCount) * EntityStateRecord::Wire::BodySampleBytes;
            }
            if (offset != payload.size())
            {
                return WorldProtocolError::InvalidLength;
            }
            const WorldProtocolError validationError = Validate(decoded);
            if (validationError != WorldProtocolError::Success)
            {
                return validationError;
            }
            *outValue = std::move(decoded);
            return WorldProtocolError::Success;
        }

        [[nodiscard]] friend bool operator==(const EntityStateBatch& left,
                                             const EntityStateBatch& right) noexcept = default;

        [[nodiscard]] static WorldProtocolError Validate(const EntityStateBatch& value) noexcept
        {
            if (value.snapshotId == 0 || value.chunkCount == 0 || value.chunkIndex >= value.chunkCount)
            {
                return WorldProtocolError::InvalidNumeric;
            }
            for (const EntityStateRecord& record : value.records)
            {
                if (record.boostState != BoostState::Off && record.boostState != BoostState::On)
                {
                    return WorldProtocolError::InvalidEnum;
                }
                if (record.entityId == 0 || record.generation == 0 || !std::isfinite(record.headPositionX) ||
                    !std::isfinite(record.headPositionY) || !std::isfinite(record.headingRadians) ||
                    !std::isfinite(record.diameter) || record.diameter <= 0.0f || record.bodyTrailSamples.empty())
                {
                    return WorldProtocolError::InvalidNumeric;
                }
                for (const EntityStateBodySample& sample : record.bodyTrailSamples)
                {
                    if (!std::isfinite(sample.positionX) || !std::isfinite(sample.positionY))
                    {
                        return WorldProtocolError::InvalidNumeric;
                    }
                }
            }
            return WorldProtocolError::Success;
        }

    private:
        static void WriteRecord(const EntityStateRecord& record, const std::size_t offset,
                                const std::span<std::byte> output) noexcept
        {
            WorldProtocolWireCodec::WriteU32(record.entityId, offset + EntityStateRecord::Wire::EntityIdOffset, output);
            WorldProtocolWireCodec::WriteU32(record.generation, offset + EntityStateRecord::Wire::GenerationOffset,
                                             output);
            WorldProtocolWireCodec::WriteF32(record.headPositionX,
                                             offset + EntityStateRecord::Wire::HeadPositionXOffset, output);
            WorldProtocolWireCodec::WriteF32(record.headPositionY,
                                             offset + EntityStateRecord::Wire::HeadPositionYOffset, output);
            WorldProtocolWireCodec::WriteF32(record.headingRadians,
                                             offset + EntityStateRecord::Wire::HeadingRadiansOffset, output);
            WorldProtocolWireCodec::WriteF32(record.diameter, offset + EntityStateRecord::Wire::DiameterOffset, output);
            WorldProtocolWireCodec::WriteU32(record.growthPoint, offset + EntityStateRecord::Wire::GrowthPointOffset,
                                             output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(record.boostState),
                                             offset + EntityStateRecord::Wire::BoostStateOffset, output);
            WorldProtocolWireCodec::WriteU16(static_cast<std::uint16_t>(record.bodyTrailSamples.size()),
                                             offset + EntityStateRecord::Wire::BodySampleCountOffset, output);
            std::size_t sampleOffset = offset + EntityStateRecord::Wire::HeaderBytes;
            for (const EntityStateBodySample& sample : record.bodyTrailSamples)
            {
                WorldProtocolWireCodec::WriteF32(sample.positionX, sampleOffset, output);
                WorldProtocolWireCodec::WriteF32(sample.positionY, sampleOffset + sizeof(float), output);
                sampleOffset += EntityStateRecord::Wire::BodySampleBytes;
            }
        }

        static void ReadRecord(const std::size_t offset, const std::uint16_t bodySampleCount,
                               const std::span<const std::byte> payload, EntityStateRecord* const outRecord)
        {
            outRecord->entityId =
                WorldProtocolWireCodec::ReadU32(offset + EntityStateRecord::Wire::EntityIdOffset, payload);
            outRecord->generation =
                WorldProtocolWireCodec::ReadU32(offset + EntityStateRecord::Wire::GenerationOffset, payload);
            outRecord->headPositionX =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::HeadPositionXOffset, payload);
            outRecord->headPositionY =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::HeadPositionYOffset, payload);
            outRecord->headingRadians =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::HeadingRadiansOffset, payload);
            outRecord->diameter =
                WorldProtocolWireCodec::ReadF32(offset + EntityStateRecord::Wire::DiameterOffset, payload);
            outRecord->growthPoint =
                WorldProtocolWireCodec::ReadU32(offset + EntityStateRecord::Wire::GrowthPointOffset, payload);
            outRecord->boostState = static_cast<BoostState>(
                WorldProtocolWireCodec::ReadU16(offset + EntityStateRecord::Wire::BoostStateOffset, payload));
            outRecord->bodyTrailSamples.resize(bodySampleCount);
            std::size_t sampleOffset = offset + EntityStateRecord::Wire::HeaderBytes;
            for (EntityStateBodySample& sample : outRecord->bodyTrailSamples)
            {
                sample.positionX = WorldProtocolWireCodec::ReadF32(sampleOffset, payload);
                sample.positionY = WorldProtocolWireCodec::ReadF32(sampleOffset + sizeof(float), payload);
                sampleOffset += EntityStateRecord::Wire::BodySampleBytes;
            }
        }
    };
} // namespace psnr::world::protocol::v2
