using System;

using PrivateServer.GameClient.Gameplay.Protocol.V1;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal readonly record struct ObserveWorldRequest : IClientGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0104;
    internal const int PayloadBytes = 2;

    uint IClientGameplayPacket.PacketType => PacketTypeValue;
    int IClientGameplayPacket.PayloadByteCount => PayloadBytes;

    public GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }

        V1WireCodec.WriteVersion(output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out ObserveWorldRequest value)
    {
        value = default;
        return V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
    }
}
