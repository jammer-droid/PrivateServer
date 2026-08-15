using System;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using IClientGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.IClientGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal enum TurnState : ushort
{
    Invalid = 0,
    Straight = 1,
    Left = 2,
    Right = 3,
}

internal readonly record struct ControlStateCommand(
    uint ControlledEntityGeneration,
    uint InputSequence,
    TurnState TurnState,
    BoostState BoostState) : IClientGameplayPacket
{
    internal const uint PacketTypeValue = 0x0103;
    internal const int PayloadBytes = 14;

    uint IClientGameplayPacket.PacketType => PacketTypeValue;
    int IClientGameplayPacket.PayloadByteCount => PayloadBytes;

    public GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (!IsValidTurnState(TurnState) || !IsValidBoostState(BoostState))
        {
            return GameplayProtocolError.InvalidEnum;
        }
        if (ControlledEntityGeneration == 0 || InputSequence == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU32(ControlledEntityGeneration, 2, output);
        V1WireCodec.WriteU32(InputSequence, 6, output);
        V1WireCodec.WriteU16((ushort)TurnState, 10, output);
        V1WireCodec.WriteU16((ushort)BoostState, 12, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out ControlStateCommand value)
    {
        value = default;
        if (payload.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 2)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        ControlStateCommand decoded = new ControlStateCommand(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            (TurnState)V1WireCodec.ReadU16(10, payload),
            (BoostState)V1WireCodec.ReadU16(12, payload));
        if (!IsValidTurnState(decoded.TurnState) || !IsValidBoostState(decoded.BoostState))
        {
            return GameplayProtocolError.InvalidEnum;
        }
        if (decoded.ControlledEntityGeneration == 0 || decoded.InputSequence == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValidTurnState(TurnState value)
    {
        return value == TurnState.Straight || value == TurnState.Left || value == TurnState.Right;
    }

    private static bool IsValidBoostState(BoostState value)
    {
        return value == BoostState.Off || value == BoostState.On;
    }
}
