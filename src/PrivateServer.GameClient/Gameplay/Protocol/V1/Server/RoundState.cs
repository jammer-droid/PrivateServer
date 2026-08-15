using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record RoundState(
    uint ServerTick,
    uint RoundId,
    RoundPhase Phase,
    uint PhaseEndsAtServerTick,
    uint ScoreToWin,
    uint WinnerPlayerId) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0186;
    internal const int PayloadBytes = 24;

    internal override uint PacketType => PacketTypeValue;

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }

        GameplayProtocolError validationError = Validate(this);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(RoundId, 6, output);
        V1WireCodec.WriteU16((ushort)Phase, 10, output);
        V1WireCodec.WriteU32(PhaseEndsAtServerTick, 12, output);
        V1WireCodec.WriteU32(ScoreToWin, 16, output);
        V1WireCodec.WriteU32(WinnerPlayerId, 20, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out RoundState? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        RoundState decoded = new RoundState(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            (RoundPhase)V1WireCodec.ReadU16(10, payload),
            V1WireCodec.ReadU32(12, payload),
            V1WireCodec.ReadU32(16, payload),
            V1WireCodec.ReadU32(20, payload));
        GameplayProtocolError validationError = Validate(decoded);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static GameplayProtocolError Validate(RoundState value)
    {
        if (!IsKnown(value.Phase))
        {
            return GameplayProtocolError.InvalidEnum;
        }
        if (value.RoundId == 0 || value.ScoreToWin == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        if (value.Phase == RoundPhase.Ended)
        {
            return value.WinnerPlayerId == 0
                ? GameplayProtocolError.InvalidNumeric
                : GameplayProtocolError.Success;
        }

        return value.WinnerPlayerId == 0
            ? GameplayProtocolError.Success
            : GameplayProtocolError.InvalidNumeric;
    }

    private static bool IsKnown(RoundPhase value)
    {
        return value == RoundPhase.Waiting ||
            value == RoundPhase.Running ||
            value == RoundPhase.Ended;
    }
}
