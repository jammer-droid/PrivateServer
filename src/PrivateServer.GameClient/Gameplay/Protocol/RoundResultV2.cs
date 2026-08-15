using System;
using System.Collections.Generic;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal sealed record RoundResult : ServerGameplayPacket
{
    internal const uint PacketTypeValue = 0x018A;
    internal const int HeaderBytes = 20;
    internal const int WinnerPlayerIdBytes = 4;
    internal const int MaximumPayloadBytes = 8186;
    internal const int MaximumWinnerCount =
        (MaximumPayloadBytes - HeaderBytes) / WinnerPlayerIdBytes;

    internal RoundResult(
        uint endTick,
        uint roundId,
        uint winningGrowthPoint,
        uint recipientFinalGrowthPoint,
        IReadOnlyList<uint> winnerPlayerIds)
    {
        ArgumentNullException.ThrowIfNull(winnerPlayerIds);
        uint[] ownedWinnerPlayerIds = new uint[winnerPlayerIds.Count];
        for (int index = 0; index < winnerPlayerIds.Count; ++index)
        {
            ownedWinnerPlayerIds[index] = winnerPlayerIds[index];
        }

        EndTick = endTick;
        RoundId = roundId;
        WinningGrowthPoint = winningGrowthPoint;
        RecipientFinalGrowthPoint = recipientFinalGrowthPoint;
        WinnerPlayerIds = Array.AsReadOnly(ownedWinnerPlayerIds);
    }

    internal override uint PacketType => PacketTypeValue;
    internal uint EndTick { get; }
    internal uint RoundId { get; }
    internal uint WinningGrowthPoint { get; }
    internal uint RecipientFinalGrowthPoint { get; }
    internal IReadOnlyList<uint> WinnerPlayerIds { get; }

    internal static int CalculatePayloadBytes(IReadOnlyList<uint> winnerPlayerIds)
    {
        ArgumentNullException.ThrowIfNull(winnerPlayerIds);
        return winnerPlayerIds.Count <= MaximumWinnerCount
            ? HeaderBytes + winnerPlayerIds.Count * WinnerPlayerIdBytes
            : 0;
    }

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        int payloadBytes = CalculatePayloadBytes(WinnerPlayerIds);
        if (payloadBytes == 0 || output.Length != payloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (!IsValid(this))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU32(EndTick, 2, output);
        V1WireCodec.WriteU32(RoundId, 6, output);
        V1WireCodec.WriteU32(WinningGrowthPoint, 10, output);
        V1WireCodec.WriteU32(RecipientFinalGrowthPoint, 14, output);
        V1WireCodec.WriteU16(checked((ushort)WinnerPlayerIds.Count), 18, output);
        int offset = HeaderBytes;
        for (int index = 0; index < WinnerPlayerIds.Count; ++index)
        {
            V1WireCodec.WriteU32(WinnerPlayerIds[index], offset, output);
            offset += WinnerPlayerIdBytes;
        }
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(ReadOnlySpan<byte> payload, out RoundResult? value)
    {
        value = null;
        if (payload.Length < HeaderBytes || payload.Length > MaximumPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 2)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        int winnerCount = V1WireCodec.ReadU16(18, payload);
        if (winnerCount > MaximumWinnerCount ||
            payload.Length != HeaderBytes + winnerCount * WinnerPlayerIdBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        uint[] winnerPlayerIds = new uint[winnerCount];
        int offset = HeaderBytes;
        for (int index = 0; index < winnerCount; ++index)
        {
            winnerPlayerIds[index] = V1WireCodec.ReadU32(offset, payload);
            offset += WinnerPlayerIdBytes;
        }

        RoundResult decoded = new RoundResult(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            V1WireCodec.ReadU32(14, payload),
            winnerPlayerIds);
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(RoundResult value)
    {
        if (value.EndTick == 0 || value.RoundId == 0 ||
            (value.WinnerPlayerIds.Count == 0 && value.WinningGrowthPoint != 0))
        {
            return false;
        }
        uint previousPlayerId = 0;
        for (int index = 0; index < value.WinnerPlayerIds.Count; ++index)
        {
            uint playerId = value.WinnerPlayerIds[index];
            if (playerId == 0 || playerId <= previousPlayerId)
            {
                return false;
            }
            previousPlayerId = playerId;
        }
        return true;
    }
}
