using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record ControlledEntityState(
    uint ServerTick,
    uint ControlledEntityGeneration,
    float PositionX,
    float PositionY,
    float VelocityX,
    float VelocityY,
    float AngleRadians) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0182;
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
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(ControlledEntityGeneration, 6, output);
        V1WireCodec.WriteF32(PositionX, 10, output);
        V1WireCodec.WriteF32(PositionY, 14, output);
        V1WireCodec.WriteF32(VelocityX, 18, output);
        V1WireCodec.WriteF32(VelocityY, 22, output);
        V1WireCodec.WriteF32(AngleRadians, 26, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out ControlledEntityState? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        ControlledEntityState decoded = new ControlledEntityState(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadF32(10, payload),
            V1WireCodec.ReadF32(14, payload),
            V1WireCodec.ReadF32(18, payload),
            V1WireCodec.ReadF32(22, payload),
            V1WireCodec.ReadF32(26, payload));
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(ControlledEntityState value)
    {
        return value.ControlledEntityGeneration != 0 &&
            float.IsFinite(value.PositionX) &&
            float.IsFinite(value.PositionY) &&
            float.IsFinite(value.VelocityX) &&
            float.IsFinite(value.VelocityY) &&
            float.IsFinite(value.AngleRadians);
    }
}
