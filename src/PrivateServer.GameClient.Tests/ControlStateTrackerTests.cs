using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Protocol.V2;
using PrivateServer.GameClient.Gameplay.Remote;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class ControlStateTrackerTests
{
    [TestMethod]
    public void BoostPressedWithoutPointRequiresReleaseBeforeAdmission()
    {
        BoostInputGate gate = new BoostInputGate();

        Assert.IsFalse(gate.Resolve(boostHeld: true, growthPoint: 0));
        Assert.IsFalse(gate.Resolve(boostHeld: true, growthPoint: 1));
        Assert.IsFalse(gate.Resolve(boostHeld: false, growthPoint: 1));
        Assert.IsTrue(gate.Resolve(boostHeld: true, growthPoint: 1));
    }

    [TestMethod]
    public void BoostDepletionWhileHeldRequiresReleaseBeforeReadmission()
    {
        BoostInputGate gate = new BoostInputGate();

        Assert.IsTrue(gate.Resolve(boostHeld: true, growthPoint: 2));
        Assert.IsFalse(gate.Resolve(boostHeld: true, growthPoint: 0));
        Assert.IsFalse(gate.Resolve(boostHeld: true, growthPoint: 1));
        Assert.IsFalse(gate.Resolve(boostHeld: false, growthPoint: 1));
        Assert.IsTrue(gate.Resolve(boostHeld: true, growthPoint: 1));
    }

    [TestMethod]
    public void CreatesCommandsOnlyForStateChangesAndTracksAcknowledgement()
    {
        ControlStateTracker tracker = new ControlStateTracker();

        Assert.AreEqual(
            ControlStateUpdateResult.CommandCreated,
            tracker.TryCreateCommand(3, false, false, false, out ControlStateCommand straight));
        Assert.AreEqual(1u, straight.InputSequence);
        Assert.AreEqual(TurnState.Straight, straight.TurnState);
        Assert.AreEqual(BoostState.Off, straight.BoostState);
        Assert.AreEqual(
            ControlStateUpdateResult.Unchanged,
            tracker.TryCreateCommand(3, false, false, false, out ControlStateCommand _));

        Assert.AreEqual(
            ControlStateUpdateResult.CommandCreated,
            tracker.TryCreateCommand(3, true, false, true, out ControlStateCommand leftBoost));
        Assert.AreEqual(2u, leftBoost.InputSequence);
        Assert.AreEqual(TurnState.Left, leftBoost.TurnState);
        Assert.AreEqual(BoostState.On, leftBoost.BoostState);
        Assert.AreEqual(ControlAcknowledgementResult.Accepted, tracker.AcceptAcknowledgement(2));
        Assert.AreEqual(2u, tracker.LastAcknowledgedSequence);
        Assert.AreEqual(ControlAcknowledgementResult.Stale, tracker.AcceptAcknowledgement(1));
        Assert.AreEqual(ControlAcknowledgementResult.BeyondSentSequence, tracker.AcceptAcknowledgement(3));

        tracker.Reset();
        Assert.AreEqual(0u, tracker.LastSentSequence);
        Assert.AreEqual(0u, tracker.LastAcknowledgedSequence);
        Assert.AreEqual(
            ControlStateUpdateResult.CommandCreated,
            tracker.TryCreateCommand(4, false, true, false, out ControlStateCommand rebound));
        Assert.AreEqual(1u, rebound.InputSequence);
        Assert.AreEqual(TurnState.Right, rebound.TurnState);
    }
}
