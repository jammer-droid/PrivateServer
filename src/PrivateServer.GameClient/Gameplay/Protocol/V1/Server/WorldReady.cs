using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record WorldReady(
    uint PlayerId,
    uint ControlledEntityId,
    uint ControlledEntityGeneration,
    uint CurrentServerTick,
    uint TickRateHz,
    uint SnapshotIntervalTicks,
    uint CommandSlackTicks,
    float ArenaMinX,
    float ArenaMinY,
    float ArenaMaxX,
    float ArenaMaxY) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0180;
    internal const int PayloadBytes = 46;

    internal override uint PacketType => PacketTypeValue;

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (!IsValid(this))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU32(PlayerId, 2, output);
        V1WireCodec.WriteU32(ControlledEntityId, 6, output);
        V1WireCodec.WriteU32(ControlledEntityGeneration, 10, output);
        V1WireCodec.WriteU32(CurrentServerTick, 14, output);
        V1WireCodec.WriteU32(TickRateHz, 18, output);
        V1WireCodec.WriteU32(SnapshotIntervalTicks, 22, output);
        V1WireCodec.WriteU32(CommandSlackTicks, 26, output);
        V1WireCodec.WriteF32(ArenaMinX, 30, output);
        V1WireCodec.WriteF32(ArenaMinY, 34, output);
        V1WireCodec.WriteF32(ArenaMaxX, 38, output);
        V1WireCodec.WriteF32(ArenaMaxY, 42, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out WorldReady? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        WorldReady decoded = new WorldReady(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            V1WireCodec.ReadU32(14, payload),
            V1WireCodec.ReadU32(18, payload),
            V1WireCodec.ReadU32(22, payload),
            V1WireCodec.ReadU32(26, payload),
            V1WireCodec.ReadF32(30, payload),
            V1WireCodec.ReadF32(34, payload),
            V1WireCodec.ReadF32(38, payload),
            V1WireCodec.ReadF32(42, payload));
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(WorldReady value)
    {
        return value.PlayerId != 0 &&
            value.ControlledEntityId != 0 &&
            value.ControlledEntityGeneration != 0 &&
            value.TickRateHz != 0 &&
            value.SnapshotIntervalTicks != 0 &&
            value.CommandSlackTicks != 0 &&
            float.IsFinite(value.ArenaMinX) &&
            float.IsFinite(value.ArenaMinY) &&
            float.IsFinite(value.ArenaMaxX) &&
            float.IsFinite(value.ArenaMaxY) &&
            value.ArenaMinX < value.ArenaMaxX &&
            value.ArenaMinY < value.ArenaMaxY;
    }
}
