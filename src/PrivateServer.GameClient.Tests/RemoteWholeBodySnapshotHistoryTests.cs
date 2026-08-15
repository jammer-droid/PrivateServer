using System;
using System.Numerics;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Replication;

using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using EntityStateBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBodySample;
using EntityStateRecordV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateRecord;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class RemoteWholeBodySnapshotHistoryTests
{
    [TestMethod]
    public void InterpolatesHeadAndArcLengthMatchedBodyWithoutExtrapolation()
    {
        RemoteWholeBodySnapshotHistory history = new RemoteWholeBodySnapshotHistory();
        history.AddOrReplace(MakeSnapshot(
            10,
            1,
            Vector2.Zero,
            Degrees(170),
            1.0f,
            5,
            BoostStateV2.Off,
            new Vector2(0, 0),
            new Vector2(10, 0)));
        history.AddOrReplace(MakeSnapshot(
            20,
            2,
            new Vector2(10, 20),
            Degrees(-170),
            3.0f,
            9,
            BoostStateV2.On,
            new Vector2(0, 10),
            new Vector2(5, 10),
            new Vector2(10, 10)));

        Assert.IsTrue(history.TrySample(15, out RemoteWholeBodyPresentationSample? interpolated));
        Assert.IsNotNull(interpolated);
        Assert.AreEqual(RemoteWholeBodySampleMode.Interpolated, interpolated.Mode);
        Assert.AreEqual(new Vector2(5, 10), interpolated.HeadPosition);
        Assert.AreEqual(MathF.PI, MathF.Abs(interpolated.HeadingRadians), 0.0001f);
        Assert.AreEqual(2.0f, interpolated.Diameter, 0.0001f);
        Assert.AreEqual(5u, interpolated.GrowthPoint);
        Assert.AreEqual(BoostStateV2.Off, interpolated.BoostState);
        Assert.AreEqual(3, interpolated.BodyTrail.Count);
        Assert.AreEqual(new Vector2(0, 5), interpolated.BodyTrail[0]);
        Assert.AreEqual(new Vector2(5, 5), interpolated.BodyTrail[1]);
        Assert.AreEqual(new Vector2(10, 5), interpolated.BodyTrail[2]);

        Assert.IsTrue(history.TrySample(25, out RemoteWholeBodyPresentationSample? held));
        Assert.IsNotNull(held);
        Assert.AreEqual(RemoteWholeBodySampleMode.Held, held.Mode);
        Assert.AreEqual(new Vector2(10, 20), held.HeadPosition);
        Assert.AreEqual(9u, held.GrowthPoint);
        Assert.AreEqual(BoostStateV2.On, held.BoostState);
        Assert.AreEqual(new Vector2(5, 10), held.BodyTrail[1]);
    }

    [TestMethod]
    public void SameTickReplacementAndCapacityKeepLatestPair()
    {
        RemoteWholeBodySnapshotHistory history = new RemoteWholeBodySnapshotHistory();
        history.AddOrReplace(MakeSnapshot(10, 1, Vector2.Zero));
        history.AddOrReplace(MakeSnapshot(10, 2, new Vector2(2, 0)));
        Assert.IsTrue(history.LatestSnapshot.HasValue);
        Assert.AreEqual(2u, history.LatestSnapshot.Value.SnapshotId);
        history.AddOrReplace(MakeSnapshot(20, 3, new Vector2(20, 0)));
        history.AddOrReplace(MakeSnapshot(30, 4, new Vector2(30, 0)));

        Assert.IsTrue(history.TrySample(15, out RemoteWholeBodyPresentationSample? held));
        Assert.IsNotNull(held);
        Assert.AreEqual(new Vector2(20, 0), held.HeadPosition);
        Assert.AreEqual(RemoteWholeBodySampleMode.Held, held.Mode);
    }

    private static RemoteWholeBodySnapshot MakeSnapshot(
        uint serverTick,
        uint snapshotId,
        Vector2 headPosition,
        float headingRadians = 0.0f,
        float diameter = 1.0f,
        uint growthPoint = 1,
        BoostStateV2 boostState = BoostStateV2.Off,
        params Vector2[] bodyTrail)
    {
        Vector2[] source = bodyTrail.Length == 0
            ? new[] { headPosition }
            : bodyTrail;
        EntityStateBodySampleV2[] samples = new EntityStateBodySampleV2[source.Length];
        for (int index = 0; index < source.Length; ++index)
        {
            samples[index] = new EntityStateBodySampleV2(source[index].X, source[index].Y);
        }
        EntityStateRecordV2 state = new EntityStateRecordV2(
            7,
            1,
            headPosition.X,
            headPosition.Y,
            headingRadians,
            diameter,
            growthPoint,
            boostState,
            samples);
        return new RemoteWholeBodySnapshot(serverTick, snapshotId, state);
    }

    private static float Degrees(float value)
    {
        return value * MathF.PI / 180.0f;
    }
}
