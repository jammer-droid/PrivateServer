using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record ControlledEntityRebind(
    uint ServerTick,
    uint PlayerId,
    uint PreviousEntityId,
    uint PreviousEntityGeneration,
    uint ControlledEntityId,
    uint ControlledEntityGeneration) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0188;
    internal const int PayloadBytes = 26;

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
        V1WireCodec.WriteU32(PlayerId, 6, output);
        V1WireCodec.WriteU32(PreviousEntityId, 10, output);
        V1WireCodec.WriteU32(PreviousEntityGeneration, 14, output);
        V1WireCodec.WriteU32(ControlledEntityId, 18, output);
        V1WireCodec.WriteU32(ControlledEntityGeneration, 22, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out ControlledEntityRebind? value)
    {
        value = null;
        GameplayProtocolError headerError =
            V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        ControlledEntityRebind decoded = new ControlledEntityRebind(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            V1WireCodec.ReadU32(14, payload),
            V1WireCodec.ReadU32(18, payload),
            V1WireCodec.ReadU32(22, payload));
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(ControlledEntityRebind value)
    {
        return value.PlayerId != 0 &&
            value.PreviousEntityId != 0 &&
            value.PreviousEntityGeneration != 0 &&
            value.ControlledEntityId != 0 &&
            value.ControlledEntityGeneration != 0 &&
            (value.PreviousEntityId != value.ControlledEntityId ||
             value.PreviousEntityGeneration != value.ControlledEntityGeneration);
    }
}
