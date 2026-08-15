using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Protocol.V2;
using PrivateServer.GameClient.Gameplay.Replication;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class RemoteEntityStateGroupAssemblerTests
{
    [TestMethod]
    public void CommitsOnlyCompleteOutOfOrderGroupInChunkOrder()
    {
        RemoteEntityStateGroupAssembler assembler = new RemoteEntityStateGroupAssembler();
        EntityStateBatch second = MakeChunk(7, 1, 2, 20);
        EntityStateBatch first = MakeChunk(7, 0, 2, 10);

        Assert.AreEqual(
            ChunkGroupAcceptResult.Buffered,
            assembler.Accept(second, out RemoteEntityStateGroup? incomplete));
        Assert.IsNull(incomplete);
        Assert.AreEqual(
            ChunkGroupAcceptResult.Duplicate,
            assembler.Accept(second, out RemoteEntityStateGroup? duplicate));
        Assert.IsNull(duplicate);
        Assert.AreEqual(
            ChunkGroupAcceptResult.Committed,
            assembler.Accept(first, out RemoteEntityStateGroup? committed));

        Assert.IsNotNull(committed);
        Assert.AreEqual(7u, committed.SnapshotId);
        Assert.AreEqual(100u, committed.ServerTick);
        Assert.AreEqual(2, committed.Records.Count);
        Assert.AreEqual(10u, committed.Records[0].EntityId);
        Assert.AreEqual(20u, committed.Records[1].EntityId);
        Assert.AreEqual(new EntityStateBodySample(20.0f, -20.0f), committed.Records[1].BodyTrailSamples[0]);
    }

    [TestMethod]
    public void NewerGroupReplacesPendingAndRejectsStaleOrInconsistentChunks()
    {
        RemoteEntityStateGroupAssembler assembler = new RemoteEntityStateGroupAssembler();
        Assert.AreEqual(
            ChunkGroupAcceptResult.Buffered,
            assembler.Accept(MakeChunk(7, 0, 2, 10), out RemoteEntityStateGroup? _));
        Assert.AreEqual(
            ChunkGroupAcceptResult.Buffered,
            assembler.Accept(MakeChunk(8, 0, 2, 10), out RemoteEntityStateGroup? _));
        Assert.AreEqual(
            ChunkGroupAcceptResult.Stale,
            assembler.Accept(MakeChunk(7, 1, 2, 20), out RemoteEntityStateGroup? _));

        EntityStateBatch inconsistent = new EntityStateBatch(
            101, 8, 1, 2, new[] { MakeRecord(20) });
        Assert.AreEqual(
            ChunkGroupAcceptResult.InvalidGroup,
            assembler.Accept(inconsistent, out RemoteEntityStateGroup? invalid));
        Assert.IsNull(invalid);
    }

    [TestMethod]
    public void RejectsDuplicateEntityAcrossChunks()
    {
        RemoteEntityStateGroupAssembler assembler = new RemoteEntityStateGroupAssembler();
        assembler.Accept(MakeChunk(9, 0, 2, 10), out RemoteEntityStateGroup? _);

        Assert.AreEqual(
            ChunkGroupAcceptResult.InvalidGroup,
            assembler.Accept(MakeChunk(9, 1, 2, 10), out RemoteEntityStateGroup? committed));
        Assert.IsNull(committed);
    }

    private static EntityStateBatch MakeChunk(
        uint snapshotId,
        ushort chunkIndex,
        ushort chunkCount,
        uint entityId)
    {
        return new EntityStateBatch(
            100,
            snapshotId,
            chunkIndex,
            chunkCount,
            new[] { MakeRecord(entityId) });
    }

    private static EntityStateRecord MakeRecord(uint entityId)
    {
        return new EntityStateRecord(
            entityId,
            1,
            entityId,
            0.0f,
            0.5f,
            1.0f,
            5,
            BoostState.Off,
            new[] { new EntityStateBodySample((float)entityId, -((float)entityId)) });
    }
}
