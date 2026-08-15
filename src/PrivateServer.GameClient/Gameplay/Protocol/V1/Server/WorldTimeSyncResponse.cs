using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record WorldTimeSyncResponse(
    uint ProbeSequence,
    uint ServerTick) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0187;
    internal const int PayloadBytes = 10;

    internal override uint PacketType => PacketTypeValue;

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU32(ProbeSequence, 2, output);
        V1WireCodec.WriteU32(ServerTick, 6, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out WorldTimeSyncResponse? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        value = new WorldTimeSyncResponse(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload));
        return GameplayProtocolError.Success;
    }
}
