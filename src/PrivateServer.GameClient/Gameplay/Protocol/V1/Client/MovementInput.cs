using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal readonly record struct MovementInput(
    uint ControlledEntityGeneration,
    uint TargetServerTick,
    short MoveX,
    short MoveY) : IClientGameplayPacketV1
{
    internal const int PayloadBytes = 14;

    uint IClientGameplayPacket.PacketType => 0x0101;
    int IClientGameplayPacket.PayloadByteCount => PayloadBytes;

    public GameplayProtocolError Encode(Span<byte> output)
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
        V1WireCodec.WriteU32(ControlledEntityGeneration, 2, output);
        V1WireCodec.WriteU32(TargetServerTick, 6, output);
        V1WireCodec.WriteI16(MoveX, 10, output);
        V1WireCodec.WriteI16(MoveY, 12, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out MovementInput value)
    {
        value = default;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        MovementInput decoded = new MovementInput(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadI16(10, payload),
            V1WireCodec.ReadI16(12, payload));
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(MovementInput value)
    {
        return value.ControlledEntityGeneration != 0 &&
            value.MoveX != short.MinValue &&
            value.MoveY != short.MinValue;
    }
}
