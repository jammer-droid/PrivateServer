using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record ScoreState(
    uint ServerTick,
    uint PlayerId,
    uint Score) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0185;
    internal const int PayloadBytes = 14;

    internal override uint PacketType => PacketTypeValue;

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (PlayerId == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(PlayerId, 6, output);
        V1WireCodec.WriteU32(Score, 10, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out ScoreState? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        ScoreState decoded = new ScoreState(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload));
        if (decoded.PlayerId == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }
}
