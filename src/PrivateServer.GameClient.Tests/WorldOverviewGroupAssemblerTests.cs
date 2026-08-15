using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Replication;
using WorldOverviewLeaderboardEntry = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewLeaderboardEntry;
using WorldOverviewPlayer = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPlayer;
using WorldOverviewPoint = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPoint;
using WorldOverviewSnapshot = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewSnapshot;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class WorldOverviewGroupAssemblerTests
{
    [TestMethod]
    public void CommitsOnlyCompleteOutOfOrderGroupInChunkOrder()
    {
        WorldOverviewGroupAssembler assembler = new WorldOverviewGroupAssembler();
        WorldOverviewSnapshot second = MakeChunk(7, 1, 2, 20, includeLeaderboard: false);
        WorldOverviewSnapshot first = MakeChunk(7, 0, 2, 10, includeLeaderboard: true);

        Assert.AreEqual(
            ChunkGroupAcceptResult.Buffered,
            assembler.Accept(second, out WorldOverviewState? incomplete));
        Assert.IsNull(incomplete);
        Assert.AreEqual(
            ChunkGroupAcceptResult.Duplicate,
            assembler.Accept(second, out WorldOverviewState? duplicate));
        Assert.IsNull(duplicate);
        Assert.AreEqual(
            ChunkGroupAcceptResult.Committed,
            assembler.Accept(first, out WorldOverviewState? committed));

        Assert.IsNotNull(committed);
        Assert.AreEqual(7u, committed.OverviewId);
        Assert.AreEqual(2, committed.Players.Count);
        Assert.AreEqual(10u, committed.Players[0].PlayerId);
        Assert.AreEqual(20u, committed.Players[1].PlayerId);
        Assert.AreEqual(1, committed.Leaderboard.Count);
        Assert.AreEqual("Player10", committed.Leaderboard[0].DisplayName);
    }

    [TestMethod]
    public void NewerGroupReplacesPendingAndRejectsStaleOrInconsistentChunks()
    {
        WorldOverviewGroupAssembler assembler = new WorldOverviewGroupAssembler();
        Assert.AreEqual(
            ChunkGroupAcceptResult.Buffered,
            assembler.Accept(MakeChunk(7, 0, 2, 10, true), out WorldOverviewState? _));
        Assert.AreEqual(
            ChunkGroupAcceptResult.Buffered,
            assembler.Accept(MakeChunk(8, 0, 2, 10, true), out WorldOverviewState? _));
        Assert.AreEqual(
            ChunkGroupAcceptResult.Stale,
            assembler.Accept(MakeChunk(7, 1, 2, 20, false), out WorldOverviewState? _));

        WorldOverviewSnapshot inconsistent = new WorldOverviewSnapshot(
            101, 8, 1, 2, -10, -10, 10, 10, 0, 0, 4,
            new[] { MakePlayer(20) },
            System.Array.Empty<WorldOverviewLeaderboardEntry>());
        Assert.AreEqual(
            ChunkGroupAcceptResult.InvalidGroup,
            assembler.Accept(inconsistent, out WorldOverviewState? invalid));
        Assert.IsNull(invalid);
    }

    [TestMethod]
    public void RejectsDuplicatePlayerAcrossChunks()
    {
        WorldOverviewGroupAssembler assembler = new WorldOverviewGroupAssembler();
        assembler.Accept(MakeChunk(9, 0, 2, 10, true), out WorldOverviewState? _);

        Assert.AreEqual(
            ChunkGroupAcceptResult.InvalidGroup,
            assembler.Accept(MakeChunk(9, 1, 2, 10, false), out WorldOverviewState? committed));
        Assert.IsNull(committed);
    }

    private static WorldOverviewSnapshot MakeChunk(
        uint overviewId,
        ushort chunkIndex,
        ushort chunkCount,
        uint playerId,
        bool includeLeaderboard)
    {
        return new WorldOverviewSnapshot(
            100, overviewId, chunkIndex, chunkCount,
            -10, -10, 10, 10, 0, 0, 4,
            new[] { MakePlayer(playerId) },
            includeLeaderboard
                ? new[] { new WorldOverviewLeaderboardEntry(1, playerId, 5, $"Player{playerId}") }
                : System.Array.Empty<WorldOverviewLeaderboardEntry>());
    }

    private static WorldOverviewPlayer MakePlayer(uint playerId)
    {
        return new WorldOverviewPlayer(
            playerId,
            5,
            new[] { new WorldOverviewPoint(playerId, 0) });
    }
}
