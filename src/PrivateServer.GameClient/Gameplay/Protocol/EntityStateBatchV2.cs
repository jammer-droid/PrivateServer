using System;
using System.Collections.Generic;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal readonly record struct EntityStateBodySample(float PositionX, float PositionY);

internal sealed record EntityStateRecord
{
    internal const int HeaderBytes = 32;
    internal const int BodySampleBytes = 8;

    internal EntityStateRecord(
        uint entityId,
        uint generation,
        float headPositionX,
        float headPositionY,
        float headingRadians,
        float diameter,
        uint growthPoint,
        BoostState boostState,
        IReadOnlyList<EntityStateBodySample> bodyTrailSamples)
    {
        ArgumentNullException.ThrowIfNull(bodyTrailSamples);
        EntityStateBodySample[] ownedSamples = new EntityStateBodySample[bodyTrailSamples.Count];
        for (int index = 0; index < bodyTrailSamples.Count; ++index)
        {
            ownedSamples[index] = bodyTrailSamples[index];
        }

        EntityId = entityId;
        Generation = generation;
        HeadPositionX = headPositionX;
        HeadPositionY = headPositionY;
        HeadingRadians = headingRadians;
        Diameter = diameter;
        GrowthPoint = growthPoint;
        BoostState = boostState;
        BodyTrailSamples = Array.AsReadOnly(ownedSamples);
    }

    internal uint EntityId { get; }
    internal uint Generation { get; }
    internal float HeadPositionX { get; }
    internal float HeadPositionY { get; }
    internal float HeadingRadians { get; }
    internal float Diameter { get; }
    internal uint GrowthPoint { get; }
    internal BoostState BoostState { get; }
    internal IReadOnlyList<EntityStateBodySample> BodyTrailSamples { get; }
}

internal sealed record EntityStateBatch : ServerGameplayPacket
{
    internal const uint PacketTypeValue = 0x0183;
    internal const int HeaderBytes = 16;
    internal const int MaximumPayloadBytes = 8186;

    internal EntityStateBatch(
        uint serverTick,
        uint snapshotId,
        ushort chunkIndex,
        ushort chunkCount,
        IReadOnlyList<EntityStateRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);
        EntityStateRecord[] ownedRecords = new EntityStateRecord[records.Count];
        for (int index = 0; index < records.Count; ++index)
        {
            EntityStateRecord record = records[index];
            ownedRecords[index] = new EntityStateRecord(
                record.EntityId,
                record.Generation,
                record.HeadPositionX,
                record.HeadPositionY,
                record.HeadingRadians,
                record.Diameter,
                record.GrowthPoint,
                record.BoostState,
                record.BodyTrailSamples);
        }

        ServerTick = serverTick;
        SnapshotId = snapshotId;
        ChunkIndex = chunkIndex;
        ChunkCount = chunkCount;
        Records = Array.AsReadOnly(ownedRecords);
    }

    internal override uint PacketType => PacketTypeValue;
    internal uint ServerTick { get; }
    internal uint SnapshotId { get; }
    internal ushort ChunkIndex { get; }
    internal ushort ChunkCount { get; }
    internal IReadOnlyList<EntityStateRecord> Records { get; }

    internal static int CalculatePayloadBytes(IReadOnlyList<EntityStateRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);
        if (records.Count > ushort.MaxValue)
        {
            return 0;
        }

        long payloadBytes = HeaderBytes;
        for (int index = 0; index < records.Count; ++index)
        {
            int bodySampleCount = records[index].BodyTrailSamples.Count;
            if (bodySampleCount > ushort.MaxValue)
            {
                return 0;
            }
            payloadBytes += EntityStateRecord.HeaderBytes + ((long)bodySampleCount * EntityStateRecord.BodySampleBytes);
            if (payloadBytes > MaximumPayloadBytes)
            {
                return 0;
            }
        }
        return checked((int)payloadBytes);
    }

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        if (Records.Count == 0)
        {
            return GameplayProtocolError.InvalidLength;
        }
        int payloadBytes = CalculatePayloadBytes(Records);
        if (payloadBytes == 0 || output.Length != payloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        GameplayProtocolError validationError = Validate(this);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(SnapshotId, 6, output);
        V1WireCodec.WriteU16(ChunkIndex, 10, output);
        V1WireCodec.WriteU16(ChunkCount, 12, output);
        V1WireCodec.WriteU16(checked((ushort)Records.Count), 14, output);
        int offset = HeaderBytes;
        for (int index = 0; index < Records.Count; ++index)
        {
            EntityStateRecord record = Records[index];
            V1WireCodec.WriteU32(record.EntityId, offset, output);
            V1WireCodec.WriteU32(record.Generation, offset + 4, output);
            V1WireCodec.WriteF32(record.HeadPositionX, offset + 8, output);
            V1WireCodec.WriteF32(record.HeadPositionY, offset + 12, output);
            V1WireCodec.WriteF32(record.HeadingRadians, offset + 16, output);
            V1WireCodec.WriteF32(record.Diameter, offset + 20, output);
            V1WireCodec.WriteU32(record.GrowthPoint, offset + 24, output);
            V1WireCodec.WriteU16((ushort)record.BoostState, offset + 28, output);
            V1WireCodec.WriteU16(checked((ushort)record.BodyTrailSamples.Count), offset + 30, output);
            offset += EntityStateRecord.HeaderBytes;
            for (int sampleIndex = 0; sampleIndex < record.BodyTrailSamples.Count; ++sampleIndex)
            {
                EntityStateBodySample sample = record.BodyTrailSamples[sampleIndex];
                V1WireCodec.WriteF32(sample.PositionX, offset, output);
                V1WireCodec.WriteF32(sample.PositionY, offset + 4, output);
                offset += EntityStateRecord.BodySampleBytes;
            }
        }
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(ReadOnlySpan<byte> payload, out EntityStateBatch? value)
    {
        value = null;
        if (payload.Length < HeaderBytes || payload.Length > MaximumPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 2)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        int recordCount = V1WireCodec.ReadU16(14, payload);
        const int MinimumRecordBytes = EntityStateRecord.HeaderBytes + EntityStateRecord.BodySampleBytes;
        if (recordCount == 0 || recordCount > (payload.Length - HeaderBytes) / MinimumRecordBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }

        EntityStateRecord[] records = new EntityStateRecord[recordCount];
        int offset = HeaderBytes;
        for (int recordIndex = 0; recordIndex < recordCount; ++recordIndex)
        {
            if (payload.Length - offset < EntityStateRecord.HeaderBytes)
            {
                return GameplayProtocolError.InvalidLength;
            }
            int bodySampleCount = V1WireCodec.ReadU16(offset + 30, payload);
            int remainingBytes = payload.Length - offset - EntityStateRecord.HeaderBytes;
            if (bodySampleCount == 0 || bodySampleCount > remainingBytes / EntityStateRecord.BodySampleBytes)
            {
                return GameplayProtocolError.InvalidLength;
            }
            EntityStateBodySample[] samples = new EntityStateBodySample[bodySampleCount];
            int sampleOffset = offset + EntityStateRecord.HeaderBytes;
            for (int sampleIndex = 0; sampleIndex < bodySampleCount; ++sampleIndex)
            {
                samples[sampleIndex] = new EntityStateBodySample(
                    V1WireCodec.ReadF32(sampleOffset, payload),
                    V1WireCodec.ReadF32(sampleOffset + 4, payload));
                sampleOffset += EntityStateRecord.BodySampleBytes;
            }
            records[recordIndex] = new EntityStateRecord(
                V1WireCodec.ReadU32(offset, payload),
                V1WireCodec.ReadU32(offset + 4, payload),
                V1WireCodec.ReadF32(offset + 8, payload),
                V1WireCodec.ReadF32(offset + 12, payload),
                V1WireCodec.ReadF32(offset + 16, payload),
                V1WireCodec.ReadF32(offset + 20, payload),
                V1WireCodec.ReadU32(offset + 24, payload),
                (BoostState)V1WireCodec.ReadU16(offset + 28, payload),
                samples);
            offset = sampleOffset;
        }
        if (offset != payload.Length)
        {
            return GameplayProtocolError.InvalidLength;
        }

        EntityStateBatch decoded = new EntityStateBatch(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU16(10, payload),
            V1WireCodec.ReadU16(12, payload),
            records);
        GameplayProtocolError validationError = Validate(decoded);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }
        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static GameplayProtocolError Validate(EntityStateBatch value)
    {
        if (value.SnapshotId == 0 || value.ChunkCount == 0 || value.ChunkIndex >= value.ChunkCount)
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        for (int recordIndex = 0; recordIndex < value.Records.Count; ++recordIndex)
        {
            EntityStateRecord record = value.Records[recordIndex];
            if (record.BoostState != BoostState.Off && record.BoostState != BoostState.On)
            {
                return GameplayProtocolError.InvalidEnum;
            }
            if (record.EntityId == 0 || record.Generation == 0 || !float.IsFinite(record.HeadPositionX) ||
                !float.IsFinite(record.HeadPositionY) || !float.IsFinite(record.HeadingRadians) ||
                !float.IsFinite(record.Diameter) || record.Diameter <= 0.0f || record.BodyTrailSamples.Count == 0)
            {
                return GameplayProtocolError.InvalidNumeric;
            }
            for (int sampleIndex = 0; sampleIndex < record.BodyTrailSamples.Count; ++sampleIndex)
            {
                EntityStateBodySample sample = record.BodyTrailSamples[sampleIndex];
                if (!float.IsFinite(sample.PositionX) || !float.IsFinite(sample.PositionY))
                {
                    return GameplayProtocolError.InvalidNumeric;
                }
            }
        }
        return GameplayProtocolError.Success;
    }
}
