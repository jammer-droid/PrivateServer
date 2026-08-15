using System;
using System.Collections.Generic;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Model;
using PrivateServer.GameClient.Gameplay.Prediction;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Remote;
using PrivateServer.GameClient.Gameplay.Replication;
using PrivateServer.NetworkRuntime.Managed;

using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using ControlStateCommandV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlStateCommand;
using ControlledEntityBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityBodySample;
using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using EntityStateBatchV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBatch;
using EntityStateBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBodySample;
using EntityStateRecordV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateRecord;
using EntitySpawnV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntitySpawn;
using JoinWorldRequestV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.JoinWorldRequest;
using RoundResultV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.RoundResult;
using TurnStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.TurnState;
using WorldOverviewLeaderboardEntryV3 = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewLeaderboardEntry;
using WorldOverviewPlayerV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPlayer;
using WorldOverviewPointV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPoint;
using WorldOverviewSnapshotV3 = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewSnapshot;
using WorldReadyV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldReady;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class RemoteGameplaySessionTests
{
    [TestMethod]
    public void ConnectSendsRequestedDisplayNameAndRejectsUnexpectedChannel()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);

        Assert.IsTrue(
            session.Connect(
                NetworkRuntimeIpv4Endpoint.Loopback(40000),
                "Player7",
                expectedChannelId: 8).Succeeded);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        session.DrainFrame(1.0);

        Assert.AreEqual(
            GameplayProtocolError.Success,
            JoinWorldRequestV2.Decode(
                transport.SentPackets[0].Payload,
                out JoinWorldRequestV2 join));
        Assert.AreEqual("Player7", join.DisplayName);

        transport.Enqueue(Packet(MakeWorldReadyV2()));
        session.DrainFrame(1.1);

        Assert.AreEqual(RemoteGameplaySessionState.Faulted, session.State);
        Assert.IsNotNull(session.LastFault);
        Assert.AreEqual(RemoteGameplaySessionFaultKind.GameplayState, session.LastFault.Kind);
        StringAssert.Contains(session.LastFault.Message, "expected=8 actual=7");
        Assert.AreEqual(1, transport.DisconnectCallCount);
    }

    [TestMethod]
    public void ConnectRejectsInvalidDisplayNameBeforeOpeningTransport()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);

        RemoteGameplaySessionOperationResult result = session.Connect(
            NetworkRuntimeIpv4Endpoint.Loopback(40000),
            "Player 7",
            expectedChannelId: 7);

        Assert.AreEqual(RemoteGameplaySessionOperationError.InvalidArgument, result.Error);
        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.AreEqual(0, transport.ConnectCallCount);
    }

    [TestMethod]
    public void ObserverAdmissionUsesObserverRequestAndSkipsPlayerTimeSync()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);

        Assert.IsTrue(session.Connect(
            NetworkRuntimeIpv4Endpoint.Loopback(40000),
            expectedChannelId: 7,
            mode: RemoteGameplaySessionMode.Observer).Succeeded);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(new RoundState(100, 1, RoundPhase.Running, 600, 10, 0)));
        transport.Enqueue(Packet(new ObserverReady(
            120,
            60,
            -10.0f,
            -10.0f,
            10.0f,
            10.0f,
            7)));

        session.DrainFrame(1.0);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.AreEqual(RemoteGameplaySessionMode.Observer, session.Mode);
        Assert.IsNotNull(session.ObserverReadyConfiguration);
        Assert.IsNull(session.ReadyConfiguration);
        Assert.AreEqual(1, transport.SentPackets.Count);
        Assert.AreEqual(ObserveWorldRequest.PacketTypeValue, transport.SentPackets[0].PacketType);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ObserveWorldRequest.Decode(
                transport.SentPackets[0].Payload,
                out ObserveWorldRequest _));
        Assert.AreEqual(
            WorldTimeSyncError.None,
            session.EstimateServerTick(1.5, out uint estimatedTick));
        Assert.AreEqual(150u, estimatedTick);

        session.DrainFrame(20.0);
        Assert.AreEqual(1, transport.SentPackets.Count);
    }

    [TestMethod]
    public void ObserverReceivesOverviewAndResultWithoutRecipientIdentity()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(
            NetworkRuntimeIpv4Endpoint.Loopback(40000),
            mode: RemoteGameplaySessionMode.Observer);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(new ObserverReady(
            120,
            60,
            -10.0f,
            -10.0f,
            10.0f,
            10.0f,
            7)));
        session.DrainFrame(1.0);

        transport.Enqueue(Packet(MakeOverviewChunk(1, 0, 1, 20, true)));
        transport.Enqueue(Packet(new RoundResultV2(700, 1, 9, 0, new uint[] { 20 })));
        session.DrainFrame(1.1);

        Assert.IsNotNull(session.LatestWorldOverview);
        Assert.IsNotNull(session.LatestRoundResult);
        Assert.IsNull(session.LatestRoundResultRecipientPlayerId);
        Assert.AreEqual(RemoteGameplaySessionState.Disconnecting, session.State);
        Assert.AreEqual(1, transport.DisconnectCallCount);
    }

    [TestMethod]
    public void RequestedDisconnectReturnsIdleWithoutTransportFault()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(
            NetworkRuntimeIpv4Endpoint.Loopback(40000),
            mode: RemoteGameplaySessionMode.Observer);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        session.DrainFrame(1.0);

        Assert.IsTrue(session.Disconnect().Succeeded);
        transport.Enqueue(RemoteGameplayTransportEvent.Disconnected(
            new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.Success, 0),
            NetworkRuntimeDisconnectReason.LocalRequested));
        session.DrainFrame(1.1);

        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.IsNull(session.LastFault);
    }

    [TestMethod]
    public void EntitySpawnV2OwnsAoiScopedPlayerLabelsAndClearsThemOnRemove()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(new EntitySpawnV2(MakeControlledSpawn(), 20, "Player7")));
        EntitySpawn remoteSpawn = MakeRemotePlayerSpawn();
        transport.Enqueue(Packet(new EntitySpawnV2(remoteSpawn, 30, "Remote30")));
        EntitySpawn anonymousSpawn = remoteSpawn with { EntityId = 31 };
        transport.Enqueue(Packet(new EntitySpawnV2(anonymousSpawn, 31, string.Empty)));
        transport.Enqueue(Packet(MakeWorldReadyV2()));
        session.DrainFrame(1.0);

        ClientWorldEntityKey controlledKey = new ClientWorldEntityKey(10, 3);
        ClientWorldEntityKey remoteKey = new ClientWorldEntityKey(remoteSpawn.EntityId, remoteSpawn.Generation);
        Assert.IsTrue(session.TryGetPlayerLabel(controlledKey, out string controlledLabel));
        Assert.AreEqual("Player7", controlledLabel);
        Assert.IsTrue(session.TryGetPlayerLabel(remoteKey, out string remoteLabel));
        Assert.AreEqual("Remote30", remoteLabel);
        Assert.IsTrue(
            session.TryGetPlayerLabel(
                new ClientWorldEntityKey(anonymousSpawn.EntityId, anonymousSpawn.Generation),
                out string anonymousLabel));
        Assert.AreEqual("PLAYER 31", anonymousLabel);

        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(1.05);

        transport.Enqueue(Packet(new EntityRemove(
            1001,
            remoteSpawn.EntityId,
            remoteSpawn.Generation,
            EntityRemoveReason.LeftAoi)));
        session.DrainFrame(1.1);
        Assert.IsFalse(session.TryGetPlayerLabel(remoteKey, out string _));
    }

    [TestMethod]
    public void GameplayTransportCapacityAbsorbsInitialResourceBurst()
    {
        NetworkRuntimeClientConfig config = NetworkRuntimeGameplayTransport.GameplayClientConfig;

        Assert.IsTrue(config.EventQueueCapacity >= 512u);
        Assert.IsTrue(
            config.EventQueueCapacity >=
            (uint)RemoteGameplaySession.DefaultMaxEventsPerFrame * 4u);
        Assert.IsTrue(config.PayloadQueueCapacity >= config.EventQueueCapacity);
    }

    [TestMethod]
    public void ReceivePressureDisconnectPreservesQueueFullFault()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        Assert.IsTrue(session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000)).Succeeded);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        session.DrainFrame(1.0);

        transport.Enqueue(RemoteGameplayTransportEvent.Disconnected(
            new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.QueueFull, 0),
            NetworkRuntimeDisconnectReason.ReceivePressure));
        session.DrainFrame(1.1);

        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.IsNotNull(session.LastFault);
        Assert.AreEqual(RemoteGameplaySessionFaultKind.TransportFailure, session.LastFault.Kind);
        StringAssert.Contains(session.LastFault.Message, nameof(NetworkRuntimeDisconnectReason.ReceivePressure));
        StringAssert.Contains(session.LastFault.Message, nameof(NetworkRuntimeErrorCode.QueueFull));
    }

    [TestMethod]
    public void RoundResultCommitsAcrossLocalDisconnectAndClearsOnReconnect()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        Assert.IsTrue(session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000)).Succeeded);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(new RoundState(10, 1, RoundPhase.Running, 600, 10, 0)));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(1.0);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(1.1);
        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);

        RoundResultV2 expected = new RoundResultV2(1200, 1, 12, 7, new uint[] { 10, 20 });
        transport.Enqueue(Packet(expected));
        session.DrainFrame(1.2);

        Assert.AreEqual(RemoteGameplaySessionState.Disconnecting, session.State);
        RoundResultV2? committed = session.LatestRoundResult;
        Assert.IsNotNull(committed);
        Assert.AreEqual(expected.EndTick, committed.EndTick);
        Assert.AreEqual(expected.RoundId, committed.RoundId);
        Assert.AreEqual(expected.WinningGrowthPoint, committed.WinningGrowthPoint);
        Assert.AreEqual(expected.RecipientFinalGrowthPoint, committed.RecipientFinalGrowthPoint);
        CollectionAssert.AreEqual(
            new List<uint>(expected.WinnerPlayerIds),
            new List<uint>(committed.WinnerPlayerIds));
        Assert.AreEqual(20u, session.LatestRoundResultRecipientPlayerId);
        Assert.AreEqual(1, transport.DisconnectCallCount);

        transport.Enqueue(RemoteGameplayTransportEvent.Disconnected(
            new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.Success, 0),
            NetworkRuntimeDisconnectReason.LocalRequested));
        session.DrainFrame(1.3);

        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.AreSame(committed, session.LatestRoundResult);
        Assert.AreEqual(20u, session.LatestRoundResultRecipientPlayerId);
        Assert.IsNull(session.ReadyConfiguration);
        Assert.IsNull(session.CurrentRoundState);

        transport.ConnectStatus = new RemoteGameplayTransportStatus(
            NetworkRuntimeErrorCode.IoFailed,
            10061);
        RemoteGameplaySessionOperationResult failedReconnect =
            session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        Assert.IsFalse(failedReconnect.Succeeded);
        Assert.IsNull(session.LatestRoundResult);
        Assert.IsNull(session.LatestRoundResultRecipientPlayerId);

        transport.ConnectStatus = new RemoteGameplayTransportStatus(
            NetworkRuntimeErrorCode.Success,
            0);
        Assert.IsTrue(session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000)).Succeeded);
        Assert.IsNull(session.LatestRoundResult);
        Assert.IsNull(session.LatestRoundResultRecipientPlayerId);
    }

    [TestMethod]
    public void HappyPathRequiresWorldReadyAndFirstTimeSyncBeforeMovement()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);

        Assert.IsTrue(session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000)).Succeeded);
        Assert.AreEqual(RemoteGameplaySessionState.Connecting, session.State);
        Assert.AreEqual(1u, session.TransportGeneration);

        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        session.DrainFrame(1.0);

        Assert.AreEqual(RemoteGameplaySessionState.AwaitingBaseline, session.State);
        Assert.AreEqual(1, transport.SentPackets.Count);
        Assert.AreEqual(0x0100u, transport.SentPackets[0].PacketType);
        CollectionAssert.AreEqual(Convert.FromHexString("02000000"), transport.SentPackets[0].Payload);

        EntitySpawn controlledSpawn = MakeControlledSpawn();
        transport.Enqueue(Packet(controlledSpawn));
        transport.Enqueue(Packet(new ScoreState(10, 20, 3)));
        transport.Enqueue(Packet(new RoundState(10, 1, RoundPhase.Running, 600, 10, 0)));
        transport.Enqueue(Packet(MakeWorldReadyV2()));
        session.DrainFrame(2.0);

        Assert.AreEqual(RemoteGameplaySessionState.AwaitingFirstTimeSync, session.State);
        Assert.IsFalse(session.CanSendMovement);
        Assert.AreEqual(1, session.BaselineSpawnCount);
        Assert.AreEqual(1, session.ScoreCount);
        Assert.IsNotNull(session.CurrentRoundState);
        Assert.IsNotNull(session.ReadyConfiguration);
        Assert.AreEqual(7u, session.ChannelId);
        Assert.AreEqual("Player7", session.DisplayName);
        Assert.AreEqual(2, transport.SentPackets.Count);
        Assert.AreEqual(0x0102u, transport.SentPackets[1].PacketType);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));

        transport.Enqueue(Packet(new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.IsTrue(session.CanSendMovement);
        Assert.AreEqual(
            WorldTimeSyncError.None,
            session.EstimateServerTick(2.25, out uint estimatedTick));
        Assert.AreEqual(1008u, estimatedTick);

        session.DrainFrame(7.0);

        Assert.AreEqual(3, transport.SentPackets.Count);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[2].Payload,
                out WorldTimeSyncRequest periodicProbe));
        Assert.AreEqual(firstProbe.ProbeSequence + 1, periodicProbe.ProbeSequence);
    }

    [TestMethod]
    public void AuthoritativeStateMayArriveWhileFirstTimeSyncKeepsMovementClosed()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        Assert.AreEqual(
            RemoteGameplaySessionState.AwaitingFirstTimeSync,
            session.State);
        Assert.IsFalse(session.CanSendMovement);
        transport.Enqueue(Packet(new ControlledEntityState(
            11,
            3,
            4.0f,
            5.0f,
            1.0f,
            0.0f,
            0.25f)));

        session.DrainFrame(2.10);

        Assert.AreEqual(
            RemoteGameplaySessionState.AwaitingFirstTimeSync,
            session.State);
        Assert.IsFalse(session.CanSendMovement);
        Assert.IsNull(session.LastFault);
        Assert.IsNotNull(session.ControlledPrediction);
        Assert.AreEqual(
            4.0f,
            session.ControlledPrediction.Value.LogicalPosition.X,
            0.0001f);
        Assert.AreEqual(
            5.0f,
            session.ControlledPrediction.Value.LogicalPosition.Y,
            0.0001f);

        WorldTimeSyncRequest.Decode(
            transport.SentPackets[1].Payload,
            out WorldTimeSyncRequest firstProbe);
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.IsTrue(session.CanSendMovement);
    }

    [TestMethod]
    public void V2AuthoritativeStateReconcilesHeadAndPreservesOwnedSnapshot()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.05);
        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);

        ControlledEntityStateV2 authoritative = new ControlledEntityStateV2(
            11,
            3,
            0,
            4.0f,
            5.0f,
            1.25f,
            2.0f,
            7,
            BoostStateV2.On,
            new[]
            {
                new ControlledEntityBodySampleV2(4.0f, 5.0f),
                new ControlledEntityBodySampleV2(3.0f, 5.0f),
            });
        transport.Enqueue(Packet(new ScoreState(10, 10, 99)));
        transport.Enqueue(Packet(authoritative));

        session.DrainFrame(2.1);

        Assert.IsNull(session.LastFault);
        Assert.IsNotNull(session.ControlledPrediction);
        Assert.AreEqual(4.0f, session.ControlledPrediction.Value.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(5.0f, session.ControlledPrediction.Value.LogicalPosition.Y, 0.0001f);
        Assert.AreEqual(1.25f, session.ControlledPrediction.Value.AngleRadians, 0.0001f);
        Assert.IsNotNull(session.ControlledRadius);
        Assert.AreEqual(1.0f, session.ControlledRadius.Value, 0.0001f);
        Assert.IsNotNull(session.LatestControlledStateV2);
        ControlledEntityStateV2 latest = session.LatestControlledStateV2!;
        Assert.AreEqual(0u, latest.LastProcessedControlSequence);
        Assert.AreEqual(7u, latest.GrowthPoint);
        Assert.AreEqual(2, latest.BodyTrailSamples.Count);
        Assert.AreEqual(
            0,
            session.BuildLeaderboard().Count,
            "V2 must not infer the channel leaderboard from legacy score packets.");

        Assert.IsTrue(session.AdvanceActiveControlFrame(2.1, 0.0f, true, false, true).Succeeded);
        Assert.AreEqual(3, transport.SentPackets.Count);
        Assert.AreEqual(ControlStateCommandV2.PacketTypeValue, transport.SentPackets[2].PacketType);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ControlStateCommandV2.Decode(transport.SentPackets[2].Payload, out ControlStateCommandV2 leftBoost));
        Assert.AreEqual(1u, leftBoost.InputSequence);
        Assert.AreEqual(TurnStateV2.Left, leftBoost.TurnState);
        Assert.AreEqual(BoostStateV2.On, leftBoost.BoostState);

        Assert.IsTrue(session.AdvanceActiveControlFrame(2.1, 0.0f, true, false, true).Succeeded);
        Assert.AreEqual(3, transport.SentPackets.Count);
        Assert.IsTrue(session.AdvanceActiveControlFrame(2.1, 0.0f, false, false, true).Succeeded);
        Assert.AreEqual(4, transport.SentPackets.Count);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ControlStateCommandV2.Decode(transport.SentPackets[3].Payload, out ControlStateCommandV2 straightBoost));
        Assert.AreEqual(2u, straightBoost.InputSequence);
        Assert.AreEqual(TurnStateV2.Straight, straightBoost.TurnState);

        ControlledEntityStateV2 acknowledged = new ControlledEntityStateV2(
            12,
            3,
            1,
            4.5f,
            5.0f,
            1.5f,
            2.0f,
            7,
            BoostStateV2.On,
            latest.BodyTrailSamples);
        transport.Enqueue(Packet(acknowledged));
        session.DrainFrame(2.2);
        Assert.IsNull(session.LastFault);
        Assert.AreEqual(2u, session.LastSentControlSequence);
        Assert.AreEqual(1u, session.LastAcknowledgedControlSequence);
    }

    [TestMethod]
    public void OverviewChunksCommitAtomicallyAndClearWithTransportGeneration()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        transport.Enqueue(Packet(MakeOverviewChunk(7, 1, 2, 20, false)));
        session.DrainFrame(2.01);
        Assert.IsNull(session.LatestWorldOverview);
        Assert.AreEqual(0, session.BuildLeaderboard().Count);

        transport.Enqueue(Packet(MakeOverviewChunk(7, 0, 2, 10, true)));
        session.DrainFrame(2.02);

        Assert.IsNotNull(session.LatestWorldOverview);
        WorldOverviewState overview = session.LatestWorldOverview!;
        Assert.AreEqual(7u, overview.OverviewId);
        Assert.AreEqual(100u, overview.ServerTick);
        Assert.AreEqual(2, overview.Players.Count);
        Assert.AreEqual(10u, overview.Players[0].PlayerId);
        Assert.AreEqual(20u, overview.Players[1].PlayerId);
        Assert.AreEqual(1, overview.Leaderboard.Count);
        Assert.AreEqual(1, session.BuildLeaderboard().Count);
        Assert.AreEqual(1u, session.BuildLeaderboard()[0].Rank);
        Assert.AreEqual(10u, session.BuildLeaderboard()[0].PlayerId);
        Assert.AreEqual(5u, session.BuildLeaderboard()[0].Score);
        Assert.AreEqual(100u, session.BuildLeaderboard()[0].ServerTick);
        Assert.AreEqual("Player10", session.BuildLeaderboard()[0].DisplayName);

        transport.Enqueue(RemoteGameplayTransportEvent.Disconnected(
            new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.Success, 0),
            NetworkRuntimeDisconnectReason.RemoteClosed));
        session.DrainFrame(2.03);

        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.IsNull(session.LatestWorldOverview);
    }

    [TestMethod]
    public void ActiveFrameSendsLatestInputOnceAndAdvancesControlledPrediction()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        Assert.AreEqual(
            RemoteGameplaySessionOperationError.InvalidState,
            session.AdvanceActiveFrame(2.0, 0.0f, 1.0f, 0.0f).Error);
        Assert.AreEqual(2, transport.SentPackets.Count);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);

        Assert.IsTrue(session.AdvanceActiveFrame(2.25, 0.0f, 3.0f, 4.0f).Succeeded);
        Assert.AreEqual(3, transport.SentPackets.Count);
        Assert.AreEqual(0x0101u, transport.SentPackets[2].PacketType);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            MovementInput.Decode(
                transport.SentPackets[2].Payload,
                out MovementInput movement));
        Assert.AreEqual(3u, movement.ControlledEntityGeneration);
        Assert.AreEqual(1008u, movement.TargetServerTick);
        Assert.AreEqual((short)19660, movement.MoveX);
        Assert.AreEqual((short)26214, movement.MoveY);
        Assert.IsNotNull(session.ControlledPrediction);
        Assert.AreEqual(
            0.05f,
            session.ControlledPrediction.Value.LogicalPosition.X,
            0.0001f);
        Assert.AreEqual(
            0.0666667f,
            session.ControlledPrediction.Value.LogicalPosition.Y,
            0.0001f);

        Assert.IsTrue(session.AdvanceActiveFrame(10.0, 7.75f, -1.0f, 0.0f).Succeeded);
        Assert.AreEqual(4, transport.SentPackets.Count);
        Assert.IsTrue(session.AdvanceActiveFrame(10.0, 0.0f, 1.0f, 0.0f).Succeeded);
        Assert.AreEqual(
            4,
            transport.SentPackets.Count,
            "A stalled frame must not produce a catch-up burst.");

        Assert.IsTrue(session.AdvanceActiveFrame(10.02, 0.02f, 0.0f, 0.0f).Succeeded);
        Assert.AreEqual(5, transport.SentPackets.Count);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            MovementInput.Decode(
                transport.SentPackets[4].Payload,
                out MovementInput idleMovement));
        Assert.AreEqual((short)0, idleMovement.MoveX);
        Assert.AreEqual((short)0, idleMovement.MoveY);
    }

    [TestMethod]
    public void SpawnThenRebindPromotesReplicaToControlledPrediction()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);
        WorldTimeSyncRequest.Decode(
            transport.SentPackets[1].Payload,
            out WorldTimeSyncRequest firstProbe);
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);

        transport.Enqueue(Packet(new EntityRemove(
            1009,
            10,
            3,
            EntityRemoveReason.Destroyed)));
        RemoteGameplayDrainResult controlledRemovalDrain = session.DrainFrame(2.25);
        Assert.IsTrue(session.IsControlledSpawnPending);
        Assert.IsFalse(session.CanSendMovement);
        Assert.AreEqual(1, controlledRemovalDrain.RemovalNotices.Count);
        Assert.AreEqual(
            EntityRemoveReason.Destroyed,
            controlledRemovalDrain.RemovalNotices[0].Reason);
        Assert.AreEqual(
            new ClientWorldEntityKey(10, 3),
            controlledRemovalDrain.RemovalNotices[0].Key);

        EntitySpawn respawn = new EntitySpawn(
            1010,
            11,
            4,
            EntityKind.Player,
            1,
            ShapeKind.Circle,
            0.5f,
            5.0f,
            4.0f,
            5.0f,
            0.0f,
            0.0f,
            0.25f);
        transport.Enqueue(Packet(respawn));
        transport.Enqueue(Packet(new ControlledEntityRebind(
            1010,
            20,
            10,
            3,
            11,
            4)));

        session.DrainFrame(2.25);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.IsNull(session.LastFault);
        Assert.IsNotNull(session.ReadyConfiguration);
        Assert.AreEqual(11u, session.ReadyConfiguration.ControlledEntityId);
        Assert.AreEqual(4u, session.ReadyConfiguration.ControlledEntityGeneration);
        Assert.AreEqual(0, session.RemoteReplicaCount);
        Assert.IsNotNull(session.ControlledPrediction);
        Assert.IsFalse(session.IsControlledSpawnPending);
        Assert.IsTrue(session.CanSendMovement);
        Assert.AreEqual(4u, session.ControlledPrediction.Value.Generation);
        Assert.AreEqual(
            4.0f,
            session.ControlledPrediction.Value.LogicalPosition.X,
            0.0001f);
        Assert.AreEqual(
            5.0f,
            session.ControlledPrediction.Value.LogicalPosition.Y,
            0.0001f);

        Assert.IsTrue(
            session.AdvanceActiveFrame(2.25, 0.0f, 0.0f, 1.0f).Succeeded);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            MovementInput.Decode(
                transport.SentPackets[^1].Payload,
                out MovementInput movement));
        Assert.AreEqual(4u, movement.ControlledEntityGeneration);
    }

    [TestMethod]
    public void ControlledDestroyedRemovalClearsPredictionControlAndGrowthUntilRebind()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);
        WorldTimeSyncRequest.Decode(
            transport.SentPackets[1].Payload,
            out WorldTimeSyncRequest firstProbe);
        transport.Enqueue(Packet(new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.05);

        transport.Enqueue(Packet(new ControlledEntityStateV2(
            11,
            3,
            0,
            4.0f,
            5.0f,
            1.25f,
            2.0f,
            7,
            BoostStateV2.On,
            new[] { new ControlledEntityBodySampleV2(4.0f, 5.0f) })));
        session.DrainFrame(2.1);
        Assert.IsTrue(session.AdvanceActiveControlFrame(2.1, 0.0f, true, false, true).Succeeded);
        Assert.AreEqual(1u, session.LastSentControlSequence);
        int sentPacketCountBeforeDeath = transport.SentPackets.Count;

        transport.Enqueue(Packet(new EntityRemove(
            12,
            10,
            3,
            EntityRemoveReason.Destroyed)));
        session.DrainFrame(2.2);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.IsTrue(session.IsControlledSpawnPending);
        Assert.IsFalse(session.CanSendMovement);
        Assert.IsNull(session.ControlledPrediction);
        Assert.IsNull(session.LatestControlledStateV2);
        Assert.AreEqual(0u, session.LastSentControlSequence);
        Assert.AreEqual(0u, session.LastAcknowledgedControlSequence);
        Assert.AreEqual(
            RemoteGameplaySessionOperationError.InvalidState,
            session.AdvanceActiveControlFrame(2.2, 0.0f, false, true, true).Error);
        Assert.AreEqual(sentPacketCountBeforeDeath, transport.SentPackets.Count);

        EntitySpawn respawn = new EntitySpawn(
            13,
            11,
            4,
            EntityKind.Player,
            1,
            ShapeKind.Circle,
            0.5f,
            5.0f,
            4.0f,
            5.0f,
            0.0f,
            0.0f,
            0.25f);
        transport.Enqueue(Packet(respawn));
        transport.Enqueue(Packet(new ControlledEntityRebind(
            13,
            20,
            10,
            3,
            11,
            4)));
        session.DrainFrame(2.3);

        Assert.IsFalse(session.IsControlledSpawnPending);
        Assert.IsTrue(session.CanSendMovement);
        Assert.IsNotNull(session.ControlledPrediction);
        Assert.AreEqual(4u, session.ControlledPrediction.Value.Generation);
        Assert.IsNull(session.LatestControlledStateV2);
        Assert.AreEqual(0u, session.LastSentControlSequence);
        Assert.AreEqual(0u, session.LastAcknowledgedControlSequence);

        transport.Enqueue(Packet(new ControlledEntityStateV2(
            14,
            4,
            0,
            4.0f,
            5.0f,
            0.25f,
            1.0f,
            0,
            BoostStateV2.Off,
            new[] { new ControlledEntityBodySampleV2(4.0f, 5.0f) })));
        session.DrainFrame(2.4);

        Assert.IsNotNull(session.LatestControlledStateV2);
        Assert.AreEqual(0u, session.LatestControlledStateV2!.GrowthPoint);
        Assert.AreEqual(BoostStateV2.Off, session.LatestControlledStateV2.BoostState);
    }

    [TestMethod]
    public void AuthoritativeControlledStateResetsLogicalPredictionAndPreservesRenderContinuity()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);
        WorldTimeSyncRequest.Decode(
            transport.SentPackets[1].Payload,
            out WorldTimeSyncRequest firstProbe);
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);
        session.AdvanceActiveFrame(2.25, 0.0f, 1.0f, 0.0f);
        Assert.IsNotNull(session.ControlledPrediction);
        float predictedBeforeCorrection =
            session.ControlledPrediction.Value.LogicalPosition.X;

        transport.Enqueue(Packet(
            new ControlledEntityState(1010, 3, 0, 0, 0, 0, 0)));
        session.DrainFrame(2.30);

        Assert.IsNotNull(session.ControlledPrediction);
        Assert.AreEqual(
            0.0f,
            session.ControlledPrediction.Value.LogicalPosition.X,
            0.0001f);
        Assert.AreEqual(
            predictedBeforeCorrection,
            session.ControlledPrediction.Value.RenderPosition.X,
            0.0001f);

        ControlledEntityPredictionSnapshot beforeStale =
            session.ControlledPrediction.Value;
        transport.Enqueue(Packet(
            new ControlledEntityState(1011, 2, 100, 100, 0, 0, 0)));
        session.DrainFrame(2.31);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.AreEqual(beforeStale, session.ControlledPrediction.Value);
    }

    [TestMethod]
    public void RemoteReplicaPacketsApplyAfterWorldReadyAndSampleFromTimeSyncTimeline()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeRemotePlayerSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        Assert.AreEqual(1, session.RemoteReplicaCount);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);
        transport.Enqueue(Packet(new EntityStateBatch(
            1010,
            new[]
            {
                new EntityStateRecord(30, 4, 10, 0, 10, 0, 0),
            })));
        session.DrainFrame(2.30);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.IsTrue(session.TrySampleRemoteReplica(
            new ClientWorldEntityKey(30, 4),
            2.30,
            out RemoteSnapshotSample sample));
        Assert.AreEqual(RemoteSnapshotSampleMode.Interpolated, sample.Mode);
        Assert.AreEqual(5.0f, sample.Position.X, 0.001f);

        transport.Enqueue(Packet(
            new EntityRemove(1011, 30, 4, EntityRemoveReason.LeftAoi)));
        transport.Enqueue(Packet(
            new EntityRemove(1011, 99, 1, EntityRemoveReason.LeftAoi)));
        RemoteGameplayDrainResult removalDrain = session.DrainFrame(2.31);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.AreEqual(0, session.RemoteReplicaCount);
        Assert.AreEqual(1, removalDrain.RemovalNotices.Count);
        Assert.AreEqual(
            EntityRemoveReason.LeftAoi,
            removalDrain.RemovalNotices[0].Reason);
        Assert.AreEqual(
            new ClientWorldEntityKey(30, 4),
            removalDrain.RemovalNotices[0].Key);
    }

    [TestMethod]
    public void RemoteWholeBodyChunksCommitToReplicaStoreOnlyWhenGroupCompletes()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeRemotePlayerSpawn()));
        transport.Enqueue(Packet(MakeRemotePlayerSpawn() with { EntityId = 31, Generation = 5 }));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);

        transport.Enqueue(Packet(new EntityStateBatchV2(
            1010,
            7,
            1,
            2,
            new[] { MakeWholeBodyState(31, 5, 31.0f) })));
        session.DrainFrame(2.30);
        Assert.IsFalse(session.TryGetLatestRemoteWholeBodySnapshot(
            new ClientWorldEntityKey(31, 5),
            out RemoteWholeBodySnapshot _));

        transport.Enqueue(Packet(new EntityStateBatchV2(
            1010,
            7,
            0,
            2,
            new[] { MakeWholeBodyState(30, 4, 30.0f) })));
        session.DrainFrame(2.31);

        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.IsTrue(session.TryGetLatestRemoteWholeBodySnapshot(
            new ClientWorldEntityKey(30, 4),
            out RemoteWholeBodySnapshot first));
        Assert.IsTrue(session.TryGetLatestRemoteWholeBodySnapshot(
            new ClientWorldEntityKey(31, 5),
            out RemoteWholeBodySnapshot second));
        Assert.AreEqual(7u, first.SnapshotId);
        Assert.AreEqual(30.0f, first.State.HeadPositionX);
        Assert.AreEqual(31.0f, second.State.BodyTrailSamples[0].PositionX);
    }

    [TestMethod]
    public void ResourceRemovalDoesNotPredictScoreAndAuthoritativePacketsDriveGameplayModel()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeResourceSpawn()));
        transport.Enqueue(Packet(new ScoreState(1000, 20, 0)));
        transport.Enqueue(Packet(new RoundState(
            1000,
            1,
            RoundPhase.Running,
            1100,
            10,
            0)));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        Assert.AreEqual(1, session.ResourceReplicaCount);
        Assert.AreEqual(0u, session.BuildLeaderboard()[0].Score);
        WorldTimeSyncRequest.Decode(
            transport.SentPackets[1].Payload,
            out WorldTimeSyncRequest firstProbe);
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);

        transport.Enqueue(Packet(
            new EntityRemove(1010, 40, 2, EntityRemoveReason.Collected)));
        RemoteGameplayDrainResult removalDrain = session.DrainFrame(2.26);

        Assert.AreEqual(0, session.ResourceReplicaCount);
        Assert.AreEqual(1, removalDrain.RemovalNotices.Count);
        Assert.AreEqual(
            EntityRemoveReason.Collected,
            removalDrain.RemovalNotices[0].Reason);
        Assert.AreEqual(
            new ClientWorldEntityKey(40, 2),
            removalDrain.RemovalNotices[0].Key);
        Assert.AreEqual(
            0u,
            session.BuildLeaderboard()[0].Score,
            "Resource removal must not make the client award score.");

        transport.Enqueue(Packet(new ScoreState(1010, 20, 1)));
        transport.Enqueue(Packet(new RoundState(
            1010,
            1,
            RoundPhase.Ended,
            1070,
            10,
            20)));
        session.DrainFrame(2.27);

        Assert.AreEqual(1u, session.BuildLeaderboard()[0].Score);
        Assert.IsNotNull(session.CurrentRoundState);
        Assert.AreEqual(RoundPhase.Ended, session.CurrentRoundState.Phase);
        Assert.AreEqual(20u, session.CurrentRoundState.WinnerPlayerId);
        Assert.IsTrue(session.TryProjectRound(
            2.27,
            out RoundPresentationState roundPresentation));
        Assert.AreEqual(RoundPhase.Ended, roundPresentation.AuthoritativeState.Phase);
        Assert.IsTrue(roundPresentation.RemainingSeconds > 0.0);
    }

    [TestMethod]
    public void DisconnectClearsPresentationStateAndReconnectConsumesFreshBaseline()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeRemotePlayerSpawn()));
        transport.Enqueue(Packet(new ScoreState(1000, 20, 3)));
        transport.Enqueue(Packet(new RoundState(
            1000,
            1,
            RoundPhase.Running,
            1100,
            10,
            0)));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);

        WorldTimeSyncRequest.Decode(
            transport.SentPackets[1].Payload,
            out WorldTimeSyncRequest firstProbe);
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, 1000)));
        session.DrainFrame(2.25);
        Assert.AreEqual(RemoteGameplaySessionState.Active, session.State);
        Assert.AreEqual(1, session.RemoteReplicaCount);
        Assert.AreEqual(1, session.ScoreCount);

        transport.Enqueue(RemoteGameplayTransportEvent.Disconnected(
            new RemoteGameplayTransportStatus(
                NetworkRuntimeErrorCode.Success,
                0),
            NetworkRuntimeDisconnectReason.RemoteClosed));
        session.DrainFrame(2.30);

        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.IsNull(session.ReadyConfiguration);
        Assert.IsNull(session.ControlledPrediction);
        Assert.AreEqual(0, session.RemoteReplicaCount);
        Assert.AreEqual(0, session.ScoreCount);
        Assert.IsNull(session.CurrentRoundState);
        Assert.IsFalse(session.TryBuildRemotePresentation(
            2.30,
            out IReadOnlyList<RemoteReplicaPresentationSnapshot> clearedSnapshots));
        Assert.AreEqual(0, clearedSnapshots.Count);

        Assert.IsTrue(session.Connect(
            NetworkRuntimeIpv4Endpoint.Loopback(40000)).Succeeded);
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn() with
        {
            Generation = 8,
        }));
        transport.Enqueue(Packet(MakeWorldReady() with
        {
            ControlledEntityGeneration = 8,
        }));
        session.DrainFrame(3.0);

        Assert.AreEqual(2u, session.TransportGeneration);
        Assert.AreEqual(
            RemoteGameplaySessionState.AwaitingFirstTimeSync,
            session.State);
        Assert.IsNotNull(session.ReadyConfiguration);
        Assert.AreEqual(
            8u,
            session.ReadyConfiguration.ControlledEntityGeneration);
        Assert.IsNotNull(session.ControlledPrediction);
        Assert.AreEqual(8u, session.ControlledPrediction.Value.Generation);
        Assert.AreEqual(0, session.RemoteReplicaCount);
        Assert.AreEqual(0, session.ScoreCount);
        Assert.IsNull(session.CurrentRoundState);
    }

    [TestMethod]
    public void DrainFrameProcessesAtMostSixtyFourEvents()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        for (int index = 0; index < 64; ++index)
        {
            transport.Enqueue(Packet(MakeControlledSpawn() with
            {
                EntityId = checked((uint)(index + 1)),
            }));
        }

        RemoteGameplayDrainResult firstDrain = session.DrainFrame(1.0);
        RemoteGameplayDrainResult secondDrain = session.DrainFrame(1.1);

        Assert.AreEqual(64, firstDrain.DrainedEventCount);
        Assert.IsTrue(firstDrain.ReachedBudget);
        Assert.AreEqual(1, secondDrain.DrainedEventCount);
        Assert.IsFalse(secondDrain.ReachedBudget);
        Assert.AreEqual(RemoteGameplaySessionState.AwaitingBaseline, session.State);
        Assert.AreEqual(64, session.BaselineSpawnCount);
        Assert.AreEqual(0, transport.PendingEventCount);
    }

    [TestMethod]
    public void ActivePacketBeforeWorldReadyIsAnOrderingFault()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(new ControlledEntityState(1, 1, 0, 0, 0, 0, 0)));

        session.DrainFrame(1.0);

        Assert.AreEqual(RemoteGameplaySessionState.Faulted, session.State);
        Assert.IsNotNull(session.LastFault);
        Assert.AreEqual(RemoteGameplaySessionFaultKind.PacketOrdering, session.LastFault.Kind);
        Assert.AreEqual(1, transport.DisconnectCallCount);
    }

    [TestMethod]
    public void MalformedPacketIsAProtocolFault()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(RemoteGameplayTransportEvent.Packet(
            WorldReady.PacketTypeValue,
            ReadOnlyMemory<byte>.Empty));

        session.DrainFrame(1.0);

        Assert.AreEqual(RemoteGameplaySessionState.Faulted, session.State);
        Assert.IsNotNull(session.LastFault);
        Assert.AreEqual(RemoteGameplaySessionFaultKind.ProtocolDecode, session.LastFault.Kind);
        Assert.AreEqual(WorldReady.PacketTypeValue, session.LastFault.PacketType);
        Assert.AreEqual(1, transport.DisconnectCallCount);
    }

    [TestMethod]
    public void ConnectionFailureReturnsToIdleAndPreservesDiagnostic()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.ConnectionFailed(
            new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.IoFailed, 10061)));

        session.DrainFrame(1.0);

        Assert.AreEqual(RemoteGameplaySessionState.Idle, session.State);
        Assert.IsNotNull(session.LastFault);
        Assert.AreEqual(RemoteGameplaySessionFaultKind.TransportFailure, session.LastFault.Kind);
        StringAssert.Contains(session.LastFault.Message, "10061");
    }

    [TestMethod]
    public void FirstTimeSyncTickOverflowKeepsMovementGateClosed()
    {
        FakeRemoteGameplayTransport transport = new FakeRemoteGameplayTransport();
        using RemoteGameplaySession session = new RemoteGameplaySession(transport);
        session.Connect(NetworkRuntimeIpv4Endpoint.Loopback(40000));
        transport.Enqueue(RemoteGameplayTransportEvent.Connected());
        transport.Enqueue(Packet(MakeControlledSpawn()));
        transport.Enqueue(Packet(MakeWorldReady()));
        session.DrainFrame(2.0);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldTimeSyncRequest.Decode(
                transport.SentPackets[1].Payload,
                out WorldTimeSyncRequest firstProbe));
        transport.Enqueue(Packet(
            new WorldTimeSyncResponse(firstProbe.ProbeSequence, uint.MaxValue)));

        session.DrainFrame(2.25);

        Assert.AreEqual(RemoteGameplaySessionState.Faulted, session.State);
        Assert.IsFalse(session.CanSendMovement);
        Assert.IsNotNull(session.LastFault);
        Assert.AreEqual(RemoteGameplaySessionFaultKind.TimeSync, session.LastFault.Kind);
        StringAssert.Contains(session.LastFault.Message, nameof(WorldTimeSyncError.TickOverflow));
    }

    private static EntitySpawn MakeControlledSpawn()
    {
        return new EntitySpawn(
            10,
            10,
            3,
            EntityKind.Player,
            1,
            ShapeKind.Circle,
            0.5f,
            5.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f);
    }

    private static WorldReady MakeWorldReady()
    {
        return new WorldReady(
            20,
            10,
            3,
            10,
            60,
            3,
            2,
            -10.0f,
            -10.0f,
            10.0f,
            10.0f);
    }

    private static WorldReadyV2 MakeWorldReadyV2()
    {
        WorldReady ready = MakeWorldReady();
        return new WorldReadyV2(
            ready.PlayerId,
            ready.ControlledEntityId,
            ready.ControlledEntityGeneration,
            ready.CurrentServerTick,
            ready.TickRateHz,
            ready.SnapshotIntervalTicks,
            ready.CommandSlackTicks,
            ready.ArenaMinX,
            ready.ArenaMinY,
            ready.ArenaMaxX,
            ready.ArenaMaxY,
            7,
            "Player7");
    }

    private static EntitySpawn MakeRemotePlayerSpawn()
    {
        return new EntitySpawn(
            1000,
            30,
            4,
            EntityKind.Player,
            2,
            ShapeKind.Circle,
            0.5f,
            5.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f);
    }

    private static EntitySpawn MakeResourceSpawn()
    {
        return new EntitySpawn(
            1000,
            40,
            2,
            EntityKind.Resource,
            3,
            ShapeKind.Circle,
            0.25f,
            0.0f,
            2.0f,
            3.0f,
            0.0f,
            0.0f,
            0.0f);
    }

    private static RemoteGameplayTransportEvent Packet(ServerGameplayPacketV1 packet)
    {
        byte[] payload;
        GameplayProtocolError encodeError;
        switch (packet)
        {
            case WorldReady value:
                payload = new byte[WorldReady.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case EntitySpawn value:
                payload = new byte[EntitySpawn.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case ControlledEntityState value:
                payload = new byte[ControlledEntityState.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case EntityStateBatch value:
                payload = new byte[
                    EntityStateBatch.CalculatePayloadBytes(value.Records.Count)];
                encodeError = value.Encode(payload);
                break;
            case EntityRemove value:
                payload = new byte[EntityRemove.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case ScoreState value:
                payload = new byte[ScoreState.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case RoundState value:
                payload = new byte[RoundState.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case ObserverReady value:
                payload = new byte[ObserverReady.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case WorldTimeSyncResponse value:
                payload = new byte[WorldTimeSyncResponse.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            case ControlledEntityRebind value:
                payload = new byte[ControlledEntityRebind.PayloadBytes];
                encodeError = value.Encode(payload);
                break;
            default:
                throw new ArgumentOutOfRangeException(
                    nameof(packet),
                    packet.GetType().Name,
                    "The test packet encoder does not support this packet.");
        }

        Assert.AreEqual(GameplayProtocolError.Success, encodeError);
        return RemoteGameplayTransportEvent.Packet(packet.PacketType, payload);
    }

    private static RemoteGameplayTransportEvent Packet(WorldReadyV2 packet)
    {
        byte[] payload = new byte[WorldReadyV2.CalculatePayloadBytes(packet.DisplayName)];
        Assert.AreEqual(GameplayProtocolError.Success, packet.Encode(payload));
        return RemoteGameplayTransportEvent.Packet(WorldReadyV2.PacketTypeValue, payload);
    }

    private static RemoteGameplayTransportEvent Packet(EntitySpawnV2 packet)
    {
        byte[] payload = new byte[EntitySpawnV2.CalculatePayloadBytes(packet.DisplayName)];
        Assert.AreEqual(GameplayProtocolError.Success, packet.Encode(payload));
        return RemoteGameplayTransportEvent.Packet(EntitySpawnV2.PacketTypeValue, payload);
    }

    private static EntityStateRecordV2 MakeWholeBodyState(
        uint entityId,
        uint generation,
        float positionX)
    {
        return new EntityStateRecordV2(
            entityId,
            generation,
            positionX,
            0.0f,
            0.5f,
            1.0f,
            5,
            BoostStateV2.Off,
            new[] { new EntityStateBodySampleV2(positionX, -positionX) });
    }

    private static WorldOverviewSnapshotV3 MakeOverviewChunk(
        uint overviewId,
        ushort chunkIndex,
        ushort chunkCount,
        uint playerId,
        bool includeLeaderboard)
    {
        return new WorldOverviewSnapshotV3(
            100,
            overviewId,
            chunkIndex,
            chunkCount,
            -10.0f,
            -10.0f,
            10.0f,
            10.0f,
            0.0f,
            0.0f,
            4.0f,
            new[]
            {
                new WorldOverviewPlayerV2(
                    playerId,
                    5,
                    new[] { new WorldOverviewPointV2(playerId, 0.0f) }),
            },
            includeLeaderboard
                ? new[] { new WorldOverviewLeaderboardEntryV3(1, playerId, 5, $"Player{playerId}") }
                : Array.Empty<WorldOverviewLeaderboardEntryV3>());
    }

    private static RemoteGameplayTransportEvent Packet(ControlledEntityStateV2 packet)
    {
        byte[] payload = new byte[
            ControlledEntityStateV2.CalculatePayloadBytes(packet.BodyTrailSamples.Count)];
        Assert.AreEqual(GameplayProtocolError.Success, packet.Encode(payload));
        return RemoteGameplayTransportEvent.Packet(packet.PacketType, payload);
    }

    private static RemoteGameplayTransportEvent Packet(WorldOverviewSnapshotV3 packet)
    {
        byte[] payload = new byte[WorldOverviewSnapshotV3.CalculatePayloadBytes(packet)];
        Assert.AreEqual(GameplayProtocolError.Success, packet.Encode(payload));
        return RemoteGameplayTransportEvent.Packet(packet.PacketType, payload);
    }

    private static RemoteGameplayTransportEvent Packet(EntityStateBatchV2 packet)
    {
        byte[] payload = new byte[EntityStateBatchV2.CalculatePayloadBytes(packet.Records)];
        Assert.AreEqual(GameplayProtocolError.Success, packet.Encode(payload));
        return RemoteGameplayTransportEvent.Packet(packet.PacketType, payload);
    }

    private static RemoteGameplayTransportEvent Packet(RoundResultV2 packet)
    {
        byte[] payload = new byte[RoundResultV2.CalculatePayloadBytes(packet.WinnerPlayerIds)];
        Assert.AreEqual(GameplayProtocolError.Success, packet.Encode(payload));
        return RemoteGameplayTransportEvent.Packet(packet.PacketType, payload);
    }

    private sealed class FakeRemoteGameplayTransport : IRemoteGameplayTransport
    {
        private readonly Queue<RemoteGameplayTransportEvent> events = new();

        internal List<SentPacket> SentPackets { get; } = new();
        internal RemoteGameplayTransportStatus ConnectStatus { get; set; } =
            new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.Success, 0);
        internal int ConnectCallCount { get; private set; }
        internal int DisconnectCallCount { get; private set; }
        internal int PendingEventCount => events.Count;

        public RemoteGameplayTransportStatus Connect(NetworkRuntimeIpv4Endpoint endpoint)
        {
            ++ConnectCallCount;
            return ConnectStatus;
        }

        public RemoteGameplayTransportStatus Disconnect()
        {
            ++DisconnectCallCount;
            return SuccessStatus();
        }

        public RemoteGameplayTransportStatus Shutdown()
        {
            return SuccessStatus();
        }

        public RemoteGameplayTransportStatus Send(uint packetType, ReadOnlySpan<byte> payload)
        {
            SentPackets.Add(new SentPacket(packetType, payload.ToArray()));
            return SuccessStatus();
        }

        public bool TryPopEvent(out RemoteGameplayTransportEvent? transportEvent)
        {
            if (events.Count == 0)
            {
                transportEvent = null;
                return false;
            }

            transportEvent = events.Dequeue();
            return true;
        }

        public void Dispose()
        {
        }

        internal void Enqueue(RemoteGameplayTransportEvent transportEvent)
        {
            events.Enqueue(transportEvent);
        }

        private static RemoteGameplayTransportStatus SuccessStatus()
        {
            return new RemoteGameplayTransportStatus(NetworkRuntimeErrorCode.Success, 0);
        }
    }

    private sealed record SentPacket(uint PacketType, byte[] Payload);
}
