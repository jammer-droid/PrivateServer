using System;
using System.Collections.Generic;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal readonly record struct EntityStateRecord(
    uint EntityId,
    uint Generation,
    float PositionX,
    float PositionY,
    float VelocityX,
    float VelocityY,
    float AngleRadians);

internal sealed record EntityStateBatch : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0183;
    internal const int HeaderBytes = 8;
    internal const int RecordBytes = 28;
    internal const int MaxRecords = 292;

    internal EntityStateBatch(uint serverTick, IReadOnlyList<EntityStateRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);

        EntityStateRecord[] ownedRecords = new EntityStateRecord[records.Count];
        for (int index = 0; index < records.Count; ++index)
        {
            ownedRecords[index] = records[index];
        }

        ServerTick = serverTick;
        Records = Array.AsReadOnly(ownedRecords);
    }

    internal override uint PacketType => PacketTypeValue;
    internal uint ServerTick { get; }
    internal IReadOnlyList<EntityStateRecord> Records { get; }

    internal static int CalculatePayloadBytes(int recordCount)
    {
        if (recordCount < 0 || recordCount > MaxRecords)
        {
            return 0;
        }

        return HeaderBytes + (recordCount * RecordBytes);
    }

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        int recordCount = Records.Count;
        if (recordCount > MaxRecords)
        {
            return GameplayProtocolError.InvalidLength;
        }

        int expectedPayloadBytes = CalculatePayloadBytes(recordCount);
        if (output.Length != expectedPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (recordCount == 0 || !AreValid(Records))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU16(checked((ushort)recordCount), 2, output);
        V1WireCodec.WriteU32(ServerTick, 4, output);
        for (int index = 0; index < recordCount; ++index)
        {
            WriteRecord(Records[index], HeaderBytes + (index * RecordBytes), output);
        }

        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out EntityStateBatch? value)
    {
        value = null;
        if (payload.Length < HeaderBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != V1WireCodec.PayloadVersion)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        int recordCount = V1WireCodec.ReadU16(2, payload);
        if (recordCount > MaxRecords)
        {
            return GameplayProtocolError.InvalidLength;
        }

        int expectedPayloadBytes = CalculatePayloadBytes(recordCount);
        if (payload.Length != expectedPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (recordCount == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        EntityStateRecord[] records = new EntityStateRecord[recordCount];
        for (int index = 0; index < recordCount; ++index)
        {
            records[index] = ReadRecord(HeaderBytes + (index * RecordBytes), payload);
        }
        if (!AreValid(records))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = new EntityStateBatch(V1WireCodec.ReadU32(4, payload), records);
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(EntityStateRecord value)
    {
        return value.EntityId != 0 &&
            value.Generation != 0 &&
            float.IsFinite(value.PositionX) &&
            float.IsFinite(value.PositionY) &&
            float.IsFinite(value.VelocityX) &&
            float.IsFinite(value.VelocityY) &&
            float.IsFinite(value.AngleRadians);
    }

    private static bool AreValid(IReadOnlyList<EntityStateRecord> records)
    {
        for (int index = 0; index < records.Count; ++index)
        {
            if (!IsValid(records[index]))
            {
                return false;
            }
        }

        return true;
    }

    private static void WriteRecord(EntityStateRecord record, int offset, Span<byte> output)
    {
        V1WireCodec.WriteU32(record.EntityId, offset, output);
        V1WireCodec.WriteU32(record.Generation, offset + 4, output);
        V1WireCodec.WriteF32(record.PositionX, offset + 8, output);
        V1WireCodec.WriteF32(record.PositionY, offset + 12, output);
        V1WireCodec.WriteF32(record.VelocityX, offset + 16, output);
        V1WireCodec.WriteF32(record.VelocityY, offset + 20, output);
        V1WireCodec.WriteF32(record.AngleRadians, offset + 24, output);
    }

    private static EntityStateRecord ReadRecord(int offset, ReadOnlySpan<byte> payload)
    {
        return new EntityStateRecord(
            V1WireCodec.ReadU32(offset, payload),
            V1WireCodec.ReadU32(offset + 4, payload),
            V1WireCodec.ReadF32(offset + 8, payload),
            V1WireCodec.ReadF32(offset + 12, payload),
            V1WireCodec.ReadF32(offset + 16, payload),
            V1WireCodec.ReadF32(offset + 20, payload),
            V1WireCodec.ReadF32(offset + 24, payload));
    }
}
