using Microsoft.VisualStudio.TestTools.UnitTesting;

using System.Collections.Generic;

using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Replication;

using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using EntityStateBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBodySample;
using EntityStateRecordV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateRecord;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class RemoteReplicaStoreTests
{
    [TestMethod]
    public void SpawnResetAndGenerationReplacementFollowEntityLifetime()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(10, 3);
        EntitySpawn first = MakeSpawn(7, 1, 10, 1.0f);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplySpawn(first, out RemoteReplicaSpawnOutcome outcome));
        Assert.AreEqual(RemoteReplicaSpawnOutcome.Added, outcome);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplySpawn(
                first with { ServerTick = 11, PositionX = 2.0f },
                out outcome));
        Assert.AreEqual(RemoteReplicaSpawnOutcome.ResetSameGeneration, outcome);
        Assert.AreEqual(1, store.Count);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplySpawn(
                MakeSpawn(7, 2, 12, 3.0f),
                out outcome));
        Assert.AreEqual(RemoteReplicaSpawnOutcome.ReplacedGeneration, outcome);
        Assert.IsFalse(store.Contains(new ClientWorldEntityKey(7, 1)));
        Assert.IsTrue(store.Contains(new ClientWorldEntityKey(7, 2)));
        Assert.AreEqual(1, store.Count);
    }

    [TestMethod]
    public void StateBatchIsAtomicAndRejectsMissingDuplicateOrStaticReplica()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(10, 1);
        store.ApplySpawn(
            MakeSpawn(7, 1, 10, 0.0f),
            out RemoteReplicaSpawnOutcome _);

        EntityStateBatch missingBatch = new EntityStateBatch(
            20,
            new[]
            {
                MakeState(7, 1, 10.0f),
                MakeState(8, 1, 20.0f),
            });
        Assert.AreEqual(
            RemoteReplicaStoreError.MissingReplica,
            store.ApplyStateBatch(missingBatch));

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplyStateBatch(new EntityStateBatch(
                15,
                new[] { MakeState(7, 1, 5.0f) })),
            "The failed batch must not partially append its first record.");

        Assert.AreEqual(
            RemoteReplicaStoreError.DuplicateBatchRecord,
            store.ApplyStateBatch(new EntityStateBatch(
                16,
                new[]
                {
                    MakeState(7, 1, 6.0f),
                    MakeState(7, 1, 7.0f),
                })));

        store.ApplySpawn(
            MakeSpawn(9, 1, 10, 0.0f, EntityKind.StaticObstacle),
            out RemoteReplicaSpawnOutcome _);
        Assert.AreEqual(
            RemoteReplicaStoreError.StaticReplicaState,
            store.ApplyStateBatch(new EntityStateBatch(
                16,
                new[] { MakeState(9, 1, 0.0f) })));
    }

    [TestMethod]
    public void SameTickStatesCoalesceAndOlderStateIsRejected()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(10, 1);
        store.ApplySpawn(
            MakeSpawn(7, 1, 10, 0.0f),
            out RemoteReplicaSpawnOutcome _);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplyStateBatch(new EntityStateBatch(
                10,
                new[] { MakeState(7, 1, 2.0f) })));
        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplyStateBatch(new EntityStateBatch(
                10,
                new[] { MakeState(7, 1, 3.0f) })));
        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.Sample(
                new ClientWorldEntityKey(7, 1),
                12,
                out RemoteSnapshotSample replaced));
        Assert.AreEqual(3.0f, replaced.Position.X, 0.0001f);

        Assert.AreEqual(
            RemoteReplicaStoreError.NonIncreasingServerTick,
            store.ApplyStateBatch(new EntityStateBatch(
                9,
                new[] { MakeState(7, 1, 4.0f) })));
    }

    [TestMethod]
    public void SamplesWithConfiguredDelayAndSafelyIgnoresMissingRemove()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(10, 1);
        store.ApplySpawn(
            MakeSpawn(7, 1, 10, 0.0f),
            out RemoteReplicaSpawnOutcome _);
        store.ApplyStateBatch(new EntityStateBatch(
            20,
            new[] { MakeState(7, 1, 10.0f, velocityX: 10.0f) }));

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.Sample(
                new ClientWorldEntityKey(7, 1),
                17,
                out RemoteSnapshotSample interpolated));
        Assert.AreEqual(RemoteSnapshotSampleMode.Interpolated, interpolated.Mode);
        Assert.AreEqual(5.0f, interpolated.Position.X, 0.0001f);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplyRemove(
                new EntityRemove(21, 99, 1, EntityRemoveReason.LeftAoi),
                out RemoteReplicaRemoveOutcome missingOutcome));
        Assert.AreEqual(RemoteReplicaRemoveOutcome.MissingIgnored, missingOutcome);
        Assert.AreEqual(1, store.Count);

        store.ApplyRemove(
            new EntityRemove(21, 7, 1, EntityRemoveReason.LeftAoi),
            out RemoteReplicaRemoveOutcome removedOutcome);
        Assert.AreEqual(RemoteReplicaRemoveOutcome.Removed, removedOutcome);
        Assert.AreEqual(0, store.Count);
    }

    [TestMethod]
    public void BuildsDeterministicPresentationSnapshotsWithReplicaMetadata()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(10, 1);
        store.ApplySpawn(
            MakeSpawn(9, 2, 10, 9.0f, EntityKind.Resource),
            out RemoteReplicaSpawnOutcome _);
        store.ApplySpawn(
            MakeSpawn(7, 3, 10, 7.0f),
            out RemoteReplicaSpawnOutcome _);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.BuildPresentationSnapshots(
                12,
                out IReadOnlyList<RemoteReplicaPresentationSnapshot> snapshots));

        Assert.AreEqual(2, snapshots.Count);
        Assert.AreEqual(new ClientWorldEntityKey(7, 3), snapshots[0].Key);
        Assert.AreEqual(EntityKind.Player, snapshots[0].EntityKind);
        Assert.AreEqual(0.5f, snapshots[0].Radius);
        Assert.IsNull(snapshots[0].WholeBody);
        Assert.AreEqual(new ClientWorldEntityKey(9, 2), snapshots[1].Key);
        Assert.AreEqual(EntityKind.Resource, snapshots[1].EntityKind);
        Assert.AreEqual(9.0f, snapshots[1].HeadPosition.X, 0.0001f);
    }

    [TestMethod]
    public void WholeBodyGroupCommitIsAtomicAndRejectsInvalidReplicaState()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(60, 2);
        store.ApplySpawn(MakeSpawn(7, 1, 10, 0.0f), out RemoteReplicaSpawnOutcome _);
        store.ApplySpawn(MakeSpawn(8, 1, 10, 0.0f), out RemoteReplicaSpawnOutcome _);
        store.ApplySpawn(
            MakeSpawn(9, 1, 10, 0.0f, EntityKind.Resource),
            out RemoteReplicaSpawnOutcome _);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplyStateGroup(MakeGroup(20, 5, MakeWholeBodyState(7, 2.0f))));
        Assert.IsTrue(store.TryGetLatestWholeBodySnapshot(
            new ClientWorldEntityKey(7, 1),
            out RemoteWholeBodySnapshot initial));

        Assert.AreEqual(
            RemoteReplicaStoreError.MissingReplica,
            store.ApplyStateGroup(MakeGroup(
                21,
                6,
                MakeWholeBodyState(7, 3.0f),
                MakeWholeBodyState(99, 9.0f))));
        Assert.IsTrue(store.TryGetLatestWholeBodySnapshot(
            new ClientWorldEntityKey(7, 1),
            out RemoteWholeBodySnapshot afterRejected));
        Assert.AreEqual(initial, afterRejected);

        Assert.AreEqual(
            RemoteReplicaStoreError.NonPlayerWholeBodyState,
            store.ApplyStateGroup(MakeGroup(21, 7, MakeWholeBodyState(9, 9.0f))));
        Assert.AreEqual(
            RemoteReplicaStoreError.NonIncreasingServerTick,
            store.ApplyStateGroup(MakeGroup(9, 8, MakeWholeBodyState(8, 8.0f))));

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.ApplyStateGroup(MakeGroup(
                21,
                9,
                MakeWholeBodyState(7, 4.0f),
                MakeWholeBodyState(8, 8.0f))));
        Assert.IsTrue(store.TryGetLatestWholeBodySnapshot(
            new ClientWorldEntityKey(7, 1),
            out RemoteWholeBodySnapshot committed));
        Assert.AreEqual(21u, committed.ServerTick);
        Assert.AreEqual(9u, committed.SnapshotId);
        Assert.AreEqual(4.0f, committed.State.HeadPositionX);
        Assert.AreEqual(
            new EntityStateBodySampleV2(4.0f, -4.0f),
            committed.State.BodyTrailSamples[0]);
    }

    [TestMethod]
    public void WholeBodySamplingUsesConfiguredInterpolationDelay()
    {
        RemoteReplicaStore store = new RemoteReplicaStore(60, 2);
        ClientWorldEntityKey key = new ClientWorldEntityKey(7, 1);
        store.ApplySpawn(MakeSpawn(7, 1, 10, 0.0f), out RemoteReplicaSpawnOutcome _);
        store.ApplyStateGroup(MakeGroup(20, 1, MakeWholeBodyState(7, 2.0f)));
        store.ApplyStateGroup(MakeGroup(24, 2, MakeWholeBodyState(7, 6.0f)));

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.SampleWholeBody(key, 26, out RemoteWholeBodyPresentationSample? sample));
        Assert.IsNotNull(sample);
        Assert.AreEqual(22.0, sample.TargetServerTick, 0.0001);
        Assert.AreEqual(RemoteWholeBodySampleMode.Interpolated, sample.Mode);
        Assert.AreEqual(4.0f, sample.HeadPosition.X, 0.0001f);
        Assert.AreEqual(4.0f, sample.BodyTrail[0].X, 0.0001f);

        Assert.AreEqual(
            RemoteReplicaStoreError.None,
            store.BuildPresentationSnapshots(
                26,
                out IReadOnlyList<RemoteReplicaPresentationSnapshot> snapshots));
        Assert.AreEqual(1, snapshots.Count);
        Assert.IsNotNull(snapshots[0].WholeBody);
        Assert.AreEqual(4.0f, snapshots[0].HeadPosition.X, 0.0001f);
        Assert.AreEqual(sample.HeadingRadians, snapshots[0].HeadingRadians);
        Assert.AreEqual(sample.Diameter * 0.5f, snapshots[0].Radius, 0.0001f);
    }

    private static RemoteEntityStateGroup MakeGroup(
        uint serverTick,
        uint snapshotId,
        params EntityStateRecordV2[] records)
    {
        return new RemoteEntityStateGroup(serverTick, snapshotId, records);
    }

    private static EntityStateRecordV2 MakeWholeBodyState(uint entityId, float positionX)
    {
        return new EntityStateRecordV2(
            entityId,
            1,
            positionX,
            0.0f,
            0.5f,
            1.0f,
            5,
            BoostStateV2.Off,
            new[] { new EntityStateBodySampleV2(positionX, -positionX) });
    }

    private static EntitySpawn MakeSpawn(
        uint entityId,
        uint generation,
        uint serverTick,
        float positionX,
        EntityKind kind = EntityKind.Player)
    {
        return new EntitySpawn(
            serverTick,
            entityId,
            generation,
            kind,
            1,
            ShapeKind.Circle,
            0.5f,
            kind == EntityKind.Player ? 5.0f : 0.0f,
            positionX,
            0,
            0,
            0,
            0);
    }

    private static EntityStateRecord MakeState(
        uint entityId,
        uint generation,
        float positionX,
        float velocityX = 0.0f)
    {
        return new EntityStateRecord(
            entityId,
            generation,
            positionX,
            0,
            velocityX,
            0,
            0);
    }
}
