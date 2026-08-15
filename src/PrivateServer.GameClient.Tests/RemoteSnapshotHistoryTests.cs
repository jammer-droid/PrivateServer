using System;
using System.Numerics;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Replication;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class RemoteSnapshotHistoryTests
{
    [TestMethod]
    public void HoldsOldestAndInterpolatesPositionVelocityAndShortestAngleArc()
    {
        RemoteSnapshotHistory history = new RemoteSnapshotHistory();
        history.Reset(new RemoteAuthoritativeSnapshot(
            10,
            Vector2.Zero,
            new Vector2(2, 0),
            Degrees(170)));
        history.Add(new RemoteAuthoritativeSnapshot(
            20,
            new Vector2(10, 20),
            new Vector2(4, 2),
            Degrees(-170)));

        Assert.AreEqual(
            RemoteSnapshotHistoryError.None,
            history.Sample(5, 10, 3, out RemoteSnapshotSample held));
        Assert.AreEqual(RemoteSnapshotSampleMode.Held, held.Mode);
        Assert.AreEqual(Vector2.Zero, held.Position);

        Assert.AreEqual(
            RemoteSnapshotHistoryError.None,
            history.Sample(15, 10, 3, out RemoteSnapshotSample interpolated));
        Assert.AreEqual(RemoteSnapshotSampleMode.Interpolated, interpolated.Mode);
        Assert.AreEqual(new Vector2(5, 10), interpolated.Position);
        Assert.AreEqual(new Vector2(3, 1), interpolated.Velocity);
        Assert.AreEqual(MathF.PI, MathF.Abs(interpolated.AngleRadians), 0.0001f);
    }

    [TestMethod]
    public void ExtrapolatesWithinLimitThenFreezesAtBound()
    {
        RemoteSnapshotHistory history = new RemoteSnapshotHistory();
        history.Reset(new RemoteAuthoritativeSnapshot(
            20,
            new Vector2(10, 0),
            new Vector2(10, 0),
            0));

        history.Sample(22, 10, 3, out RemoteSnapshotSample extrapolated);
        Assert.AreEqual(RemoteSnapshotSampleMode.Extrapolated, extrapolated.Mode);
        Assert.AreEqual(12.0f, extrapolated.Position.X, 0.0001f);
        Assert.AreEqual(10.0f, extrapolated.Velocity.X, 0.0001f);

        history.Sample(25, 10, 3, out RemoteSnapshotSample frozen);
        Assert.AreEqual(RemoteSnapshotSampleMode.Frozen, frozen.Mode);
        Assert.AreEqual(13.0f, frozen.Position.X, 0.0001f);
        Assert.AreEqual(Vector2.Zero, frozen.Velocity);
    }

    [TestMethod]
    public void RejectsDuplicateAndOutOfOrderServerTicks()
    {
        RemoteSnapshotHistory history = new RemoteSnapshotHistory();
        history.Reset(new RemoteAuthoritativeSnapshot(
            10,
            Vector2.Zero,
            Vector2.Zero,
            0));

        Assert.AreEqual(
            RemoteSnapshotHistoryError.NonIncreasingServerTick,
            history.Add(new RemoteAuthoritativeSnapshot(
                10,
                Vector2.One,
                Vector2.Zero,
                0)));
        Assert.AreEqual(
            RemoteSnapshotHistoryError.NonIncreasingServerTick,
            history.Add(new RemoteAuthoritativeSnapshot(
                9,
                Vector2.One,
                Vector2.Zero,
                0)));
        Assert.AreEqual(1, history.Count);
    }

    private static float Degrees(float value)
    {
        return value * MathF.PI / 180.0f;
    }
}
