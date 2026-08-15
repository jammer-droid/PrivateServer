using Microsoft.VisualStudio.TestTools.UnitTesting;
using PrivateServer.GameClient.Gameplay.Flow;
using PrivateServer.GameClient.Gameplay.Remote;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayFlowTests
{
    [TestMethod]
    public void ConnectionProgressesThroughJoiningSpawnPendingAndPlaying()
    {
        GameplayFlow flow = new GameplayFlow();

        Assert.AreEqual(GameplayFlowState.ChannelSelect, flow.State);
        Assert.IsTrue(flow.TrySelectChannel());
        Assert.AreEqual(GameplayFlowState.PlayerSetup, flow.State);
        Assert.IsTrue(flow.TryBeginConnection());
        Assert.AreEqual(GameplayFlowState.Connecting, flow.State);

        Assert.IsTrue(flow.TryApply(Observe(RemoteGameplaySessionState.Connecting)));
        Assert.AreEqual(GameplayFlowState.Connecting, flow.State);
        Assert.IsTrue(flow.TryApply(Observe(RemoteGameplaySessionState.AwaitingBaseline)));
        Assert.AreEqual(GameplayFlowState.Joining, flow.State);
        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.AwaitingFirstTimeSync,
            hasControlledEntity: true)));
        Assert.AreEqual(GameplayFlowState.SpawnPending, flow.State);
        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Active,
            hasControlledEntity: true)));
        Assert.AreEqual(GameplayFlowState.Playing, flow.State);
    }

    [TestMethod]
    public void ControlledDeathAndRebindMoveBetweenPlayingAndSpawnPending()
    {
        GameplayFlow flow = BeginPlayingFlow();

        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Active,
            isControlledSpawnPending: true)));
        Assert.AreEqual(GameplayFlowState.SpawnPending, flow.State);

        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Active,
            hasControlledEntity: true)));
        Assert.AreEqual(GameplayFlowState.Playing, flow.State);
    }

    [TestMethod]
    public void CommittedResultSurvivesDisconnectingAndIdleObservations()
    {
        GameplayFlow flow = BeginPlayingFlow();

        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Disconnecting,
            hasResult: true)));
        Assert.AreEqual(GameplayFlowState.Result, flow.State);
        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Idle,
            hasResult: true)));
        Assert.AreEqual(GameplayFlowState.Result, flow.State);
    }

    [TestMethod]
    public void FailureRequiresExplicitReturnBeforeAnotherConnection()
    {
        GameplayFlow flow = new GameplayFlow();
        Assert.IsTrue(flow.TrySelectChannel());
        Assert.IsTrue(flow.TryBeginConnection());

        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Idle,
            hasFault: true)));
        Assert.AreEqual(GameplayFlowState.Error, flow.State);
        Assert.IsFalse(flow.TryBeginConnection());

        Assert.IsTrue(flow.TryReturnToChannelSelect());
        Assert.AreEqual(GameplayFlowState.ChannelSelect, flow.State);
        Assert.IsTrue(flow.TrySelectChannel());
        Assert.IsTrue(flow.TryBeginConnection());
        Assert.AreEqual(GameplayFlowState.Connecting, flow.State);
    }

    [TestMethod]
    public void UnexpectedIdleWithoutResultBecomesError()
    {
        GameplayFlow flow = BeginPlayingFlow();

        Assert.IsTrue(flow.TryApply(Observe(RemoteGameplaySessionState.Idle)));

        Assert.AreEqual(GameplayFlowState.Error, flow.State);
    }

    [TestMethod]
    public void ExitIsAvailableOnlyFromTerminalUserChoiceScreens()
    {
        GameplayFlow connectingFlow = new GameplayFlow();
        Assert.IsTrue(connectingFlow.TrySelectChannel());
        Assert.IsTrue(connectingFlow.TryBeginConnection());
        Assert.IsFalse(connectingFlow.TryExit());

        GameplayFlow channelFlow = new GameplayFlow();
        Assert.IsTrue(channelFlow.TryExit());
        Assert.AreEqual(GameplayFlowState.Exiting, channelFlow.State);
        Assert.IsFalse(channelFlow.TryApply(Observe(RemoteGameplaySessionState.Idle)));

        GameplayFlow resultFlow = BeginPlayingFlow();
        Assert.IsTrue(resultFlow.TryApply(Observe(
            RemoteGameplaySessionState.Disconnecting,
            hasResult: true)));
        Assert.IsTrue(resultFlow.TryExit());
        Assert.AreEqual(GameplayFlowState.Exiting, resultFlow.State);
    }

    private static GameplayFlow BeginPlayingFlow()
    {
        GameplayFlow flow = new GameplayFlow();
        Assert.IsTrue(flow.TrySelectChannel());
        Assert.IsTrue(flow.TryBeginConnection());
        Assert.IsTrue(flow.TryApply(Observe(
            RemoteGameplaySessionState.Active,
            hasControlledEntity: true)));
        Assert.AreEqual(GameplayFlowState.Playing, flow.State);
        return flow;
    }

    private static GameplayFlowObservation Observe(
        RemoteGameplaySessionState sessionState,
        bool isControlledSpawnPending = false,
        bool hasControlledEntity = false,
        bool hasResult = false,
        bool hasFault = false)
    {
        return new GameplayFlowObservation(
            sessionState,
            isControlledSpawnPending,
            hasControlledEntity,
            hasResult,
            hasFault);
    }
}
