using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record ObserverReady(
    uint CurrentServerTick,
    uint TickRateHz,
    float ArenaMinX,
    float ArenaMinY,
    float ArenaMaxX,
    float ArenaMaxY,
    uint ChannelId) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x018B;
    internal const int PayloadBytes = 30;

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
        V1WireCodec.WriteU32(CurrentServerTick, 2, output);
        V1WireCodec.WriteU32(TickRateHz, 6, output);
        V1WireCodec.WriteF32(ArenaMinX, 10, output);
        V1WireCodec.WriteF32(ArenaMinY, 14, output);
        V1WireCodec.WriteF32(ArenaMaxX, 18, output);
        V1WireCodec.WriteF32(ArenaMaxY, 22, output);
        V1WireCodec.WriteU32(ChannelId, 26, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out ObserverReady? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        ObserverReady decoded = new ObserverReady(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadF32(10, payload),
            V1WireCodec.ReadF32(14, payload),
            V1WireCodec.ReadF32(18, payload),
            V1WireCodec.ReadF32(22, payload),
            V1WireCodec.ReadU32(26, payload));
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(ObserverReady value)
    {
        return value.TickRateHz != 0 &&
            value.ChannelId != 0 &&
            float.IsFinite(value.ArenaMinX) &&
            float.IsFinite(value.ArenaMinY) &&
            float.IsFinite(value.ArenaMaxX) &&
            float.IsFinite(value.ArenaMaxY) &&
            value.ArenaMinX < value.ArenaMaxX &&
            value.ArenaMinY < value.ArenaMaxY;
    }
}
