using System;
using System.Collections.Generic;

using WorldOverviewLeaderboardEntry = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewLeaderboardEntry;
using WorldOverviewPlayer = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPlayer;
using WorldOverviewPoint = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPoint;
using WorldOverviewSnapshot = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewSnapshot;

namespace PrivateServer.GameClient.Gameplay.Replication;

internal sealed record WorldOverviewState
{
    internal WorldOverviewState(
        uint serverTick,
        uint overviewId,
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
        WorldOverviewPlayer[] ownedPlayers = new WorldOverviewPlayer[players.Count];
        for (int index = 0; index < players.Count; ++index)
        {
            WorldOverviewPlayer player = players[index];
            ownedPlayers[index] = new WorldOverviewPlayer(
                player.PlayerId,
                player.GrowthPoint,
                player.BodySamples);
        }
        WorldOverviewLeaderboardEntry[] ownedLeaderboard = new WorldOverviewLeaderboardEntry[leaderboard.Count];
        for (int index = 0; index < leaderboard.Count; ++index)
        {
            ownedLeaderboard[index] = leaderboard[index];
        }

        ServerTick = serverTick;
        OverviewId = overviewId;
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

    internal uint ServerTick { get; }
    internal uint OverviewId { get; }
    internal float MapMinX { get; }
    internal float MapMinY { get; }
    internal float MapMaxX { get; }
    internal float MapMaxY { get; }
    internal float ActiveAreaCenterX { get; }
    internal float ActiveAreaCenterY { get; }
    internal float ActiveAreaRadius { get; }
    internal IReadOnlyList<WorldOverviewPlayer> Players { get; }
    internal IReadOnlyList<WorldOverviewLeaderboardEntry> Leaderboard { get; }
}

// 여러 Packet 으로 분할된 overview 정보를 하나로 합침
// - stale chunk 는 버림
// - 현재 처리 중인 chunk 보다 최신 데이터가 오면 최신으로 갱신
// - serverTick, overviewId, chunkCount, MapBounds, ActiveArea 가 동일해야 같은 chunk
internal sealed class WorldOverviewGroupAssembler : ChunkGroupAssembler<WorldOverviewSnapshot, WorldOverviewState>
{
    protected override uint GroupId(WorldOverviewSnapshot chunk) => chunk.OverviewId;
    protected override ushort ChunkIndex(WorldOverviewSnapshot chunk) => chunk.ChunkIndex;
    protected override ushort ChunkCount(WorldOverviewSnapshot chunk) => chunk.ChunkCount;

    protected override bool TryCommit(
        IReadOnlyList<WorldOverviewSnapshot> orderedChunks,
        out WorldOverviewState? committed)
    {
        committed = null;
        List<WorldOverviewPlayer> players = new List<WorldOverviewPlayer>();
        HashSet<uint> playerIds = new HashSet<uint>();
        for (int chunkIndex = 0; chunkIndex < orderedChunks.Count; ++chunkIndex)
        {
            WorldOverviewSnapshot chunk = orderedChunks[chunkIndex];
            for (int playerIndex = 0; playerIndex < chunk.Players.Count; ++playerIndex)
            {
                WorldOverviewPlayer player = chunk.Players[playerIndex];
                if (!playerIds.Add(player.PlayerId))
                {
                    return false;
                }
                players.Add(player);
            }
        }

        WorldOverviewSnapshot first = orderedChunks[0];
        committed = new WorldOverviewState(
            first.ServerTick,
            first.OverviewId,
            first.MapMinX,
            first.MapMinY,
            first.MapMaxX,
            first.MapMaxY,
            first.ActiveAreaCenterX,
            first.ActiveAreaCenterY,
            first.ActiveAreaRadius,
            players,
            first.Leaderboard);
        return true;
    }

    protected override bool ChunksEqual(WorldOverviewSnapshot left, WorldOverviewSnapshot right)
    {
        if (!MetadataEqual(left, right) ||
            left.ChunkIndex != right.ChunkIndex ||
            left.Players.Count != right.Players.Count ||
            left.Leaderboard.Count != right.Leaderboard.Count)
        {
            return false;
        }
        for (int playerIndex = 0; playerIndex < left.Players.Count; ++playerIndex)
        {
            WorldOverviewPlayer leftPlayer = left.Players[playerIndex];
            WorldOverviewPlayer rightPlayer = right.Players[playerIndex];
            if (leftPlayer.PlayerId != rightPlayer.PlayerId ||
                leftPlayer.GrowthPoint != rightPlayer.GrowthPoint ||
                leftPlayer.BodySamples.Count != rightPlayer.BodySamples.Count)
            {
                return false;
            }
            for (int sampleIndex = 0; sampleIndex < leftPlayer.BodySamples.Count; ++sampleIndex)
            {
                WorldOverviewPoint leftPoint = leftPlayer.BodySamples[sampleIndex];
                WorldOverviewPoint rightPoint = rightPlayer.BodySamples[sampleIndex];
                if (leftPoint != rightPoint)
                {
                    return false;
                }
            }
        }
        for (int index = 0; index < left.Leaderboard.Count; ++index)
        {
            if (left.Leaderboard[index] != right.Leaderboard[index])
            {
                return false;
            }
        }
        return true;
    }

    protected override bool MetadataEqual(WorldOverviewSnapshot left, WorldOverviewSnapshot right)
    {
        return left.ServerTick == right.ServerTick &&
               left.OverviewId == right.OverviewId &&
               left.ChunkCount == right.ChunkCount &&
               left.MapMinX == right.MapMinX &&
               left.MapMinY == right.MapMinY &&
               left.MapMaxX == right.MapMaxX &&
               left.MapMaxY == right.MapMaxY &&
               left.ActiveAreaCenterX == right.ActiveAreaCenterX &&
               left.ActiveAreaCenterY == right.ActiveAreaCenterY &&
               left.ActiveAreaRadius == right.ActiveAreaRadius;
    }

}
