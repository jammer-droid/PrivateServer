using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal readonly record struct WorldTimeSyncRequest(
    uint ProbeSequence) : IClientGameplayPacketV1
{
    internal const int PayloadBytes = 6;

    uint IClientGameplayPacket.PacketType => 0x0102;
    int IClientGameplayPacket.PayloadByteCount => PayloadBytes;

    public GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU32(ProbeSequence, 2, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out WorldTimeSyncRequest value)
    {
        value = default;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        value = new WorldTimeSyncRequest(V1WireCodec.ReadU32(2, payload));
        return GameplayProtocolError.Success;
    }
}
