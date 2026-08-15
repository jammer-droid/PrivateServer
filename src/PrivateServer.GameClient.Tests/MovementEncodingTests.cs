using System.Numerics;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Remote;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class MovementEncodingTests
{
    [TestMethod]
    public void NormalizesVectorAndRoundsAxesAwayFromZero()
    {
        Assert.IsTrue(MovementSendScheduler.TryNormalizeAndEncode(
            3.0f,
            4.0f,
            out Vector2 normalized,
            out short encodedX,
            out short encodedY));

        Assert.AreEqual(0.6f, normalized.X, 0.000001f);
        Assert.AreEqual(0.8f, normalized.Y, 0.000001f);
        Assert.AreEqual((short)19660, encodedX);
        Assert.AreEqual((short)26214, encodedY);

        float positiveHalfStep = 0.5f / short.MaxValue;
        Assert.IsTrue(MovementSendScheduler.TryNormalizeAndEncode(
            positiveHalfStep,
            -positiveHalfStep,
            out normalized,
            out encodedX,
            out encodedY));
        Assert.AreEqual((short)1, encodedX);
        Assert.AreEqual((short)-1, encodedY);
    }

    [TestMethod]
    public void NegativeFullAxisNeverProducesReservedInt16Minimum()
    {
        Assert.IsTrue(MovementSendScheduler.TryNormalizeAndEncode(
            -1.0f,
            0.0f,
            out Vector2 _,
            out short encodedX,
            out short encodedY));

        Assert.AreEqual((short)-32767, encodedX);
        Assert.AreEqual((short)0, encodedY);
        Assert.AreNotEqual(short.MinValue, encodedX);
    }

    [TestMethod]
    public void MissedDeadlineProducesAtMostOneSendAndRealignsFromCurrentTime()
    {
        MovementSendScheduler scheduler = new MovementSendScheduler();
        scheduler.Start(0.0, 10);

        Assert.IsTrue(scheduler.TryConsumeDeadline(0.0));
        Assert.IsFalse(scheduler.TryConsumeDeadline(0.05));
        Assert.IsTrue(scheduler.TryConsumeDeadline(1.0));
        Assert.IsFalse(scheduler.TryConsumeDeadline(1.0));
        Assert.IsFalse(scheduler.TryConsumeDeadline(1.09));
        Assert.IsTrue(scheduler.TryConsumeDeadline(1.10));
    }
}
