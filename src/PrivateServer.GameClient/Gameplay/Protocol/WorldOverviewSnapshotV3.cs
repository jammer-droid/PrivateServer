using System;
using System.Collections.Generic;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;
using WorldOverviewPlayer = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPlayer;
using WorldOverviewPoint = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPoint;

namespace PrivateServer.GameClient.Gameplay.Protocol.V3;

internal readonly record struct WorldOverviewLeaderboardEntry(
    ushort Rank,
    uint PlayerId,
    uint GrowthPoint,
    string DisplayName);

internal sealed record WorldOverviewSnapshot : ServerGameplayPacket
{
    internal const uint PacketTypeValue = 0x0189;
    internal const int HeaderBytes = 46;
    internal const int PlayerHeaderBytes = 10;
    internal const int PointBytes = 8;
    internal const int LeaderboardEntryHeaderBytes = 12;
    internal const int MaximumPayloadBytes = 8186;
    internal const int MaximumLeaderboardEntryCount = 10;

    internal WorldOverviewSnapshot(
        uint serverTick,
        uint overviewId,
        ushort chunkIndex,
        ushort chunkCount,
        float mapMinX,
        float mapMinY,
        float mapMaxX,
        float mapMaxY,
        float activeAreaCenterX,
        float activeAreaCenterY,
        float activeAreaRadius,
        IReadOnlyList<WorldOverviewPlayer> players,
        IReadOnlyList<WorldOverviewLeaderboardEntry> leaderboard)
    {
        ArgumentNullException.ThrowIfNull(players);
        ArgumentNullException.ThrowIfNull(leaderboard);
        WorldOverviewPlayer[] ownedPlayers = new WorldOverviewPlayer[players.Count];
        for (int index = 0; index < players.Count; ++index)
        {
            WorldOverviewPlayer player = players[index];
            ownedPlayers[index] = new WorldOverviewPlayer(player.PlayerId, player.GrowthPoint, player.BodySamples);
        }
        WorldOverviewLeaderboardEntry[] ownedLeaderboard = new WorldOverviewLeaderboardEntry[leaderboard.Count];
        for (int index = 0; index < leaderboard.Count; ++index)
        {
            ownedLeaderboard[index] = leaderboard[index];
        }

        ServerTick = serverTick;
        OverviewId = overviewId;
        ChunkIndex = chunkIndex;
        ChunkCount = chunkCount;
        MapMinX = mapMinX;
        MapMinY = mapMinY;
        MapMaxX = mapMaxX;
        MapMaxY = mapMaxY;
        ActiveAreaCenterX = activeAreaCenterX;
        ActiveAreaCenterY = activeAreaCenterY;
        ActiveAreaRadius = activeAreaRadius;
        Players = Array.AsReadOnly(ownedPlayers);
        Leaderboard = Array.AsReadOnly(ownedLeaderboard);
    }

    internal override uint PacketType => PacketTypeValue;
    internal uint ServerTick { get; }
    internal uint OverviewId { get; }
    internal ushort ChunkIndex { get; }
    internal ushort ChunkCount { get; }
    internal float MapMinX { get; }
    internal float MapMinY { get; }
    internal float MapMaxX { get; }
    internal float MapMaxY { get; }
    internal float ActiveAreaCenterX { get; }
    internal float ActiveAreaCenterY { get; }
    internal float ActiveAreaRadius { get; }
    internal IReadOnlyList<WorldOverviewPlayer> Players { get; }
    internal IReadOnlyList<WorldOverviewLeaderboardEntry> Leaderboard { get; }

    internal static int CalculatePayloadBytes(WorldOverviewSnapshot value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Players.Count > ushort.MaxValue || value.Leaderboard.Count > MaximumLeaderboardEntryCount)
        {
            return 0;
        }
        long size = HeaderBytes;
        for (int index = 0; index < value.Players.Count; ++index)
        {
            int sampleCount = value.Players[index].BodySamples.Count;
            if (sampleCount == 0 || sampleCount > ushort.MaxValue)
            {
                return 0;
            }
            size += PlayerHeaderBytes + ((long)sampleCount * PointBytes);
        }
        for (int index = 0; index < value.Leaderboard.Count; ++index)
        {
            string? displayName = value.Leaderboard[index].DisplayName;
            if (displayName is null || displayName.Length > PlayerDisplayNameRules.MaximumByteCount)
            {
                return 0;
            }
            size += LeaderboardEntryHeaderBytes + displayName.Length;
        }
        return size <= MaximumPayloadBytes ? checked((int)size) : 0;
    }

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        int payloadBytes = CalculatePayloadBytes(this);
        if (payloadBytes == 0 || output.Length != payloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (!IsValidNumeric(this))
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        for (int index = 0; index < Leaderboard.Count; ++index)
        {
            if (!PlayerDisplayNameRules.IsValid(Leaderboard[index].DisplayName))
            {
                return GameplayProtocolError.InvalidArgument;
            }
        }

        V1WireCodec.WriteU16(3, 0, output);
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(OverviewId, 6, output);
        V1WireCodec.WriteU16(ChunkIndex, 10, output);
        V1WireCodec.WriteU16(ChunkCount, 12, output);
        V1WireCodec.WriteF32(MapMinX, 14, output);
        V1WireCodec.WriteF32(MapMinY, 18, output);
        V1WireCodec.WriteF32(MapMaxX, 22, output);
        V1WireCodec.WriteF32(MapMaxY, 26, output);
        V1WireCodec.WriteF32(ActiveAreaCenterX, 30, output);
        V1WireCodec.WriteF32(ActiveAreaCenterY, 34, output);
        V1WireCodec.WriteF32(ActiveAreaRadius, 38, output);
        V1WireCodec.WriteU16(checked((ushort)Players.Count), 42, output);
        V1WireCodec.WriteU16(checked((ushort)Leaderboard.Count), 44, output);

        int offset = HeaderBytes;
        for (int playerIndex = 0; playerIndex < Players.Count; ++playerIndex)
        {
            WorldOverviewPlayer player = Players[playerIndex];
            V1WireCodec.WriteU32(player.PlayerId, offset, output);
            V1WireCodec.WriteU32(player.GrowthPoint, offset + 4, output);
            V1WireCodec.WriteU16(checked((ushort)player.BodySamples.Count), offset + 8, output);
            offset += PlayerHeaderBytes;
            for (int sampleIndex = 0; sampleIndex < player.BodySamples.Count; ++sampleIndex)
            {
                WorldOverviewPoint point = player.BodySamples[sampleIndex];
                V1WireCodec.WriteF32(point.PositionX, offset, output);
                V1WireCodec.WriteF32(point.PositionY, offset + 4, output);
                offset += PointBytes;
            }
        }
        for (int index = 0; index < Leaderboard.Count; ++index)
        {
            WorldOverviewLeaderboardEntry entry = Leaderboard[index];
            V1WireCodec.WriteU16(entry.Rank, offset, output);
            V1WireCodec.WriteU32(entry.PlayerId, offset + 2, output);
            V1WireCodec.WriteU32(entry.GrowthPoint, offset + 6, output);
            V1WireCodec.WriteU16(checked((ushort)entry.DisplayName.Length), offset + 10, output);
            offset += LeaderboardEntryHeaderBytes;
            PlayerDisplayNameRules.Write(entry.DisplayName, output.Slice(offset, entry.DisplayName.Length));
            offset += entry.DisplayName.Length;
        }
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(ReadOnlySpan<byte> payload, out WorldOverviewSnapshot? value)
    {
        value = null;
        if (payload.Length < HeaderBytes || payload.Length > MaximumPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 3)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        int playerCount = V1WireCodec.ReadU16(42, payload);
        int leaderboardCount = V1WireCodec.ReadU16(44, payload);
        const int MinimumPlayerBytes = PlayerHeaderBytes + PointBytes;
        if (playerCount > (payload.Length - HeaderBytes) / MinimumPlayerBytes ||
            leaderboardCount > MaximumLeaderboardEntryCount)
        {
            return GameplayProtocolError.InvalidLength;
        }
        int offset = HeaderBytes;
        WorldOverviewPlayer[] players = new WorldOverviewPlayer[playerCount];
        for (int playerIndex = 0; playerIndex < playerCount; ++playerIndex)
        {
            if (payload.Length - offset < PlayerHeaderBytes)
            {
                return GameplayProtocolError.InvalidLength;
            }
            uint playerId = V1WireCodec.ReadU32(offset, payload);
            uint growthPoint = V1WireCodec.ReadU32(offset + 4, payload);
            int sampleCount = V1WireCodec.ReadU16(offset + 8, payload);
            offset += PlayerHeaderBytes;
            long sampleBytes = (long)sampleCount * PointBytes;
            if (sampleCount == 0 || sampleBytes > payload.Length - offset)
            {
                return GameplayProtocolError.InvalidLength;
            }
            WorldOverviewPoint[] samples = new WorldOverviewPoint[sampleCount];
            for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                samples[sampleIndex] = new WorldOverviewPoint(
                    V1WireCodec.ReadF32(offset, payload),
                    V1WireCodec.ReadF32(offset + 4, payload));
                offset += PointBytes;
            }
            players[playerIndex] = new WorldOverviewPlayer(playerId, growthPoint, samples);
        }

        WorldOverviewLeaderboardEntry[] leaderboard = new WorldOverviewLeaderboardEntry[leaderboardCount];
        for (int index = 0; index < leaderboardCount; ++index)
        {
            if (payload.Length - offset < LeaderboardEntryHeaderBytes)
            {
                return GameplayProtocolError.InvalidLength;
            }
            ushort rank = V1WireCodec.ReadU16(offset, payload);
            uint playerId = V1WireCodec.ReadU32(offset + 2, payload);
            uint growthPoint = V1WireCodec.ReadU32(offset + 6, payload);
            int displayNameByteCount = V1WireCodec.ReadU16(offset + 10, payload);
            offset += LeaderboardEntryHeaderBytes;
            if (displayNameByteCount > payload.Length - offset)
            {
                return GameplayProtocolError.InvalidLength;
            }
            ReadOnlySpan<byte> displayNameBytes = payload.Slice(offset, displayNameByteCount);
            if (!PlayerDisplayNameRules.IsValid(displayNameBytes))
            {
                return GameplayProtocolError.InvalidArgument;
            }
            leaderboard[index] = new WorldOverviewLeaderboardEntry(
                rank, playerId, growthPoint, PlayerDisplayNameRules.Decode(displayNameBytes));
            offset += displayNameByteCount;
        }
        if (offset != payload.Length)
        {
            return GameplayProtocolError.InvalidLength;
        }

        WorldOverviewSnapshot decoded = new WorldOverviewSnapshot(
            V1WireCodec.ReadU32(2, payload), V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU16(10, payload), V1WireCodec.ReadU16(12, payload),
            V1WireCodec.ReadF32(14, payload), V1WireCodec.ReadF32(18, payload),
            V1WireCodec.ReadF32(22, payload), V1WireCodec.ReadF32(26, payload),
            V1WireCodec.ReadF32(30, payload), V1WireCodec.ReadF32(34, payload),
            V1WireCodec.ReadF32(38, payload), players, leaderboard);
        if (!IsValidNumeric(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValidNumeric(WorldOverviewSnapshot value)
    {
        if (value.OverviewId == 0 || value.ChunkCount == 0 || value.ChunkIndex >= value.ChunkCount ||
            (value.ChunkIndex != 0 && value.Leaderboard.Count != 0) || !float.IsFinite(value.MapMinX) ||
            !float.IsFinite(value.MapMinY) || !float.IsFinite(value.MapMaxX) || !float.IsFinite(value.MapMaxY) ||
            value.MapMinX >= value.MapMaxX || value.MapMinY >= value.MapMaxY ||
            !float.IsFinite(value.ActiveAreaCenterX) || !float.IsFinite(value.ActiveAreaCenterY) ||
            !float.IsFinite(value.ActiveAreaRadius) || value.ActiveAreaRadius <= 0.0f)
        {
            return false;
        }
        for (int playerIndex = 0; playerIndex < value.Players.Count; ++playerIndex)
        {
            WorldOverviewPlayer player = value.Players[playerIndex];
            if (player.PlayerId == 0 || player.BodySamples.Count == 0)
            {
                return false;
            }
            for (int sampleIndex = 0; sampleIndex < player.BodySamples.Count; ++sampleIndex)
            {
                WorldOverviewPoint point = player.BodySamples[sampleIndex];
                if (!float.IsFinite(point.PositionX) || !float.IsFinite(point.PositionY))
                {
                    return false;
                }
            }
        }
        for (int index = 0; index < value.Leaderboard.Count; ++index)
        {
            WorldOverviewLeaderboardEntry entry = value.Leaderboard[index];
            if (entry.Rank == 0 || entry.PlayerId == 0)
            {
                return false;
            }
        }
        return true;
    }
}
