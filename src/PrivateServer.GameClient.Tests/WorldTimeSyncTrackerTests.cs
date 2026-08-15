using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Remote;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class WorldTimeSyncTrackerTests
{
    [TestMethod]
    public void FirstValidResponseOpensEstimationAndUsesCeilingHalfRtt()
    {
        WorldTimeSyncTracker tracker = new WorldTimeSyncTracker();

        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.BeginProbe(2.0, out WorldTimeSyncRequest request));
        Assert.AreEqual(1u, request.ProbeSequence);
        Assert.IsTrue(tracker.HasOutstandingProbe);

        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.AcceptResponse(
                new WorldTimeSyncResponse(request.ProbeSequence, 1000),
                2.25,
                60));
        Assert.IsFalse(tracker.HasOutstandingProbe);
        Assert.IsTrue(tracker.HasValidSample);

        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.EstimateServerTick(2.25, 60, out uint estimatedTick));
        Assert.AreEqual(1008u, estimatedTick);
        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.EstimateServerTick(2.50, 60, out estimatedTick));
        Assert.AreEqual(1023u, estimatedTick);
    }

    [TestMethod]
    public void RetainsLatestEightSamplesAndSelectsMinimumRttWithLatestTieBreak()
    {
        WorldTimeSyncTracker tracker = new WorldTimeSyncTracker();

        for (uint sequence = 1; sequence <= 10; ++sequence)
        {
            double sendTime = sequence * 10.0;
            double roundTripTime = sequence == 8 || sequence == 10 ? 0.25 : 0.5;
            Assert.AreEqual(
                WorldTimeSyncError.None,
                tracker.BeginProbe(sendTime, out WorldTimeSyncRequest request));
            Assert.AreEqual(sequence, request.ProbeSequence);
            Assert.AreEqual(
                WorldTimeSyncError.None,
                tracker.AcceptResponse(
                    new WorldTimeSyncResponse(sequence, sequence * 100),
                    sendTime + roundTripTime,
                    60));
        }

        Assert.AreEqual(WorldTimeSyncTracker.DefaultSampleCapacity, tracker.SampleCount);
        Assert.IsNotNull(tracker.SelectedSample);
        Assert.AreEqual(10u, tracker.SelectedSample.Value.ProbeSequence);
        Assert.AreEqual(0.25, tracker.SelectedSample.Value.RoundTripTimeSeconds, 0.000001);
    }

    [TestMethod]
    public void RejectsMismatchedResponsesAndResetClearsGenerationLocalState()
    {
        WorldTimeSyncTracker tracker = new WorldTimeSyncTracker();

        Assert.AreEqual(
            WorldTimeSyncError.NoOutstandingProbe,
            tracker.AcceptResponse(new WorldTimeSyncResponse(1, 10), 1.0, 60));
        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.BeginProbe(1.0, out WorldTimeSyncRequest request));
        Assert.AreEqual(
            WorldTimeSyncError.SequenceMismatch,
            tracker.AcceptResponse(
                new WorldTimeSyncResponse(request.ProbeSequence + 1, 10),
                1.1,
                60));
        Assert.IsTrue(tracker.HasOutstandingProbe);

        tracker.Reset();

        Assert.IsFalse(tracker.HasOutstandingProbe);
        Assert.IsFalse(tracker.HasValidSample);
        Assert.AreEqual(0, tracker.SampleCount);
        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.BeginProbe(2.0, out WorldTimeSyncRequest resetRequest));
        Assert.AreEqual(1u, resetRequest.ProbeSequence);
    }

    [TestMethod]
    public void EstimatedTickRejectsUintOverflow()
    {
        WorldTimeSyncTracker tracker = new WorldTimeSyncTracker();
        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.BeginProbe(1.0, out WorldTimeSyncRequest request));
        Assert.AreEqual(
            WorldTimeSyncError.None,
            tracker.AcceptResponse(
                new WorldTimeSyncResponse(request.ProbeSequence, uint.MaxValue),
                1.25,
                60));

        Assert.AreEqual(
            WorldTimeSyncError.TickOverflow,
            tracker.EstimateServerTick(1.25, 60, out uint _));
    }
}
