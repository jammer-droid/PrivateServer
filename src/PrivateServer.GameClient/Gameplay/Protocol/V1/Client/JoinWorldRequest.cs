using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

// Join 요청은 payload version 외에 별도 gameplay data를 포함하지 않는다.
internal readonly record struct JoinWorldRequest : IClientGameplayPacketV1
{
    internal const int PayloadBytes = 2;

    uint IClientGameplayPacket.PacketType => 0x0100;
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
        out JoinWorldRequest value)
    {
        value = default;
        return V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
    }
}
