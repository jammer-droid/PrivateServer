using PrivateServer.GameClient.Gameplay.Model;
using PrivateServer.GameClient.Gameplay.Prediction;
using PrivateServer.GameClient.Gameplay.Protocol;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Replication;
using PrivateServer.NetworkRuntime.Managed;
using System;
using System.Collections.Generic;
using System.Numerics;

using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using ControlStateCommandV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlStateCommand;
using EntityStateBatchV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBatch;
using EntitySpawnV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntitySpawn;
using JoinWorldRequestV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.JoinWorldRequest;
using RoundResultV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.RoundResult;
using WorldReadyV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldReady;
using WorldOverviewLeaderboardEntryV3 = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewLeaderboardEntry;
using WorldOverviewSnapshotV3 = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewSnapshot;

namespace PrivateServer.GameClient.Gameplay.Remote;

internal sealed class RemoteGameplaySession : IDisposable
{
    internal const int DefaultMaxEventsPerFrame = 64;
    internal const double DefaultTimeSyncIntervalSeconds = 5.0;

    private readonly IRemoteGameplayTransport transport;
    private readonly int maxEventsPerFrame;
    private readonly double timeSyncIntervalSeconds;
    private readonly IClientPredictionPhysics predictionPhysics;
    private readonly Dictionary<(uint EntityId, uint Generation), EntitySpawn> baselineSpawns = new();
    private readonly Dictionary<(uint EntityId, uint Generation), EntitySpawnV2> playerIdentitySpawns = new();
    private readonly AuthoritativeGameplayState gameplayState = new();
    private readonly WorldTimeSyncTracker timeSyncTracker = new();
    private readonly MovementSendScheduler movementScheduler = new();
    private readonly ControlStateTracker controlStateTracker = new();
    private readonly BoostInputGate boostInputGate = new();
    private readonly WorldOverviewGroupAssembler worldOverviewAssembler = new();
    private readonly RemoteEntityStateGroupAssembler remoteEntityStateAssembler = new();
    private readonly List<ClientStaticObstacle> staticObstacles = new();
    private readonly List<RemoteGameplayRemovalNotice> frameRemovalNotices = new();
    private IReadOnlyList<LeaderboardEntry> roundResultLeaderboard =
        Array.Empty<LeaderboardEntry>();
    private bool disposed;
    private uint transportGeneration;
    private double? lastDrainTimeSeconds;
    private ControlledEntityPrediction? controlledPrediction;
    private RemoteReplicaStore? remoteReplicas;
    private string requestedDisplayName = string.Empty;
    private uint expectedChannelId;
    private double? observerReadyReceivedAtSeconds;
    private bool localDisconnectRequested;

    internal RemoteGameplaySession(
        int maxEventsPerFrame = DefaultMaxEventsPerFrame,
        double timeSyncIntervalSeconds = DefaultTimeSyncIntervalSeconds)
        : this(
            new NetworkRuntimeGameplayTransport(),
            maxEventsPerFrame,
            timeSyncIntervalSeconds)
    {
    }

    internal RemoteGameplaySession(
        IRemoteGameplayTransport transport,
        int maxEventsPerFrame = DefaultMaxEventsPerFrame,
        double timeSyncIntervalSeconds = DefaultTimeSyncIntervalSeconds,
        IClientPredictionPhysics? predictionPhysics = null)
    {
        ArgumentNullException.ThrowIfNull(transport);
        if (maxEventsPerFrame <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxEventsPerFrame),
                "The per-frame event budget must be positive.");
        }
        if (!double.IsFinite(timeSyncIntervalSeconds) || timeSyncIntervalSeconds <= 0.0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(timeSyncIntervalSeconds),
                "The time-sync interval must be finite and positive.");
        }

        this.transport = transport;
        this.maxEventsPerFrame = maxEventsPerFrame;
        this.timeSyncIntervalSeconds = timeSyncIntervalSeconds;
        this.predictionPhysics =
            predictionPhysics ?? new ClientStaticCollisionAdapter();
    }

    internal RemoteGameplaySessionState State { get; private set; }
    internal uint TransportGeneration => transportGeneration;
    internal bool CanSendMovement =>
        State == RemoteGameplaySessionState.Active &&
        !IsControlledSpawnPending &&
        controlledPrediction is not null;
    internal RemoteGameplaySessionFault? LastFault { get; private set; }
    internal WorldReady? ReadyConfiguration { get; private set; }
    internal ObserverReady? ObserverReadyConfiguration { get; private set; }
    internal RemoteGameplaySessionMode Mode { get; private set; }
    internal uint ChannelId { get; private set; }
    internal string DisplayName { get; private set; } = string.Empty;
    internal bool IsControlledSpawnPending { get; private set; }
    internal RoundState? CurrentRoundState => gameplayState.Round;
    internal int BaselineSpawnCount => baselineSpawns.Count;
    internal int ScoreCount => gameplayState.ScoreCount;
    internal ControlledEntityPredictionSnapshot? ControlledPrediction =>
        controlledPrediction?.Snapshot;
    internal float? ControlledRadius => controlledPrediction?.Radius;
    internal ControlledEntityStateV2? LatestControlledStateV2 { get; private set; }
    internal WorldOverviewState? LatestWorldOverview { get; private set; }
    internal RoundResultV2? LatestRoundResult { get; private set; }
    internal uint? LatestRoundResultRecipientPlayerId { get; private set; }
    internal bool UsesControlProtocolV2 { get; private set; }
    internal uint LastSentControlSequence => controlStateTracker.LastSentSequence;
    internal uint LastAcknowledgedControlSequence => controlStateTracker.LastAcknowledgedSequence;
    internal int RemoteReplicaCount => remoteReplicas?.Count ?? 0;
    internal int ResourceReplicaCount =>
        remoteReplicas?.CountByKind(EntityKind.Resource) ?? 0;
    internal string? LastReplicaAnomaly { get; private set; }

    internal bool TryGetLatestRemoteWholeBodySnapshot(
        ClientWorldEntityKey key,
        out RemoteWholeBodySnapshot snapshot)
    {
        snapshot = default;
        return remoteReplicas is not null &&
            remoteReplicas.TryGetLatestWholeBodySnapshot(key, out snapshot);
    }

    internal RemoteGameplaySessionOperationResult Connect(
        NetworkRuntimeIpv4Endpoint endpoint,
        string displayName = "",
        uint expectedChannelId = 0,
        RemoteGameplaySessionMode mode = RemoteGameplaySessionMode.Player)
    {
        if (disposed)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.Disposed,
                null);
        }
        if (State != RemoteGameplaySessionState.Idle)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.InvalidState,
                null);
        }
        if (mode == RemoteGameplaySessionMode.Player &&
            !PlayerDisplayNameRules.IsValid(displayName))
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.InvalidArgument,
                null);
        }

        LatestRoundResult = null;
        LatestRoundResultRecipientPlayerId = null;
        roundResultLeaderboard = Array.Empty<LeaderboardEntry>();
        ResetGenerationLocalState();
        Mode = mode;
        requestedDisplayName = displayName;
        this.expectedChannelId = expectedChannelId;
        LastFault = null;
        RemoteGameplayTransportStatus status = transport.Connect(endpoint);
        if (!status.Succeeded)
        {
            LastFault = new RemoteGameplaySessionFault(
                RemoteGameplaySessionFaultKind.TransportFailure,
                transportGeneration,
                null,
                $"Connect failed with {status.ErrorCode} ({status.NativeErrorCode}).");
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.TransportFailure,
                status);
        }

        if (transportGeneration == uint.MaxValue)
        {
            LastFault = new RemoteGameplaySessionFault(
                RemoteGameplaySessionFaultKind.TransportFailure,
                transportGeneration,
                null,
                "The local transport generation counter overflowed.");
            transport.Disconnect();
            State = RemoteGameplaySessionState.Faulted;
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.TransportFailure,
                status);
        }

        ++transportGeneration;
        State = RemoteGameplaySessionState.Connecting;
        return new RemoteGameplaySessionOperationResult(
            RemoteGameplaySessionOperationError.None,
            status);
    }

    internal RemoteGameplaySessionOperationResult Disconnect()
    {
        if (disposed)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.Disposed,
                null);
        }
        if (State == RemoteGameplaySessionState.Idle ||
            State == RemoteGameplaySessionState.Disconnecting)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.InvalidState,
                null);
        }

        RemoteGameplayTransportStatus status = transport.Disconnect();
        if (!status.Succeeded)
        {
            Fault(
                RemoteGameplaySessionFaultKind.TransportFailure,
                null,
                $"Disconnect failed with {status.ErrorCode} ({status.NativeErrorCode}).",
                requestDisconnect: false);
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.TransportFailure,
                status);
        }

        localDisconnectRequested = true;
        State = RemoteGameplaySessionState.Disconnecting;
        return new RemoteGameplaySessionOperationResult(
            RemoteGameplaySessionOperationError.None,
            status);
    }

    internal RemoteGameplayDrainResult DrainFrame(double nowSeconds)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        frameRemovalNotices.Clear();

        if (!IsValidClock(nowSeconds))
        {
            Fault(
                RemoteGameplaySessionFaultKind.Clock,
                null,
                "Frame time must be finite, non-negative, and monotonic.",
                requestDisconnect: State != RemoteGameplaySessionState.Idle);
            return new RemoteGameplayDrainResult(
                0,
                false,
                State,
                Array.Empty<RemoteGameplayRemovalNotice>());
        }
        lastDrainTimeSeconds = nowSeconds;

        int drainedEventCount = 0;
        while (drainedEventCount < maxEventsPerFrame &&
               transport.TryPopEvent(out RemoteGameplayTransportEvent? transportEvent))
        {
            if (transportEvent is null)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.TransportFailure,
                    null,
                    "The transport returned a null event.",
                    requestDisconnect: true);
                break;
            }

            ++drainedEventCount;
            ProcessTransportEvent(transportEvent, nowSeconds);
            if (State == RemoteGameplaySessionState.Faulted)
            {
                break;
            }
        }

        if (State == RemoteGameplaySessionState.Active &&
            Mode == RemoteGameplaySessionMode.Player &&
            timeSyncTracker.IsPeriodicProbeDue(nowSeconds, timeSyncIntervalSeconds))
        {
            BeginAndSendTimeSyncProbe(nowSeconds);
        }

        return new RemoteGameplayDrainResult(
            drainedEventCount,
            drainedEventCount == maxEventsPerFrame,
            State,
            frameRemovalNotices.ToArray());
    }

    internal WorldTimeSyncError EstimateServerTick(
        double nowSeconds,
        out uint estimatedServerTick)
    {
        if (Mode == RemoteGameplaySessionMode.Observer)
        {
            WorldTimeSyncError observerError = EstimateObserverTimeline(
                nowSeconds,
                out double observerTimeline);
            if (observerError != WorldTimeSyncError.None)
            {
                estimatedServerTick = 0;
                return observerError;
            }
            if (observerTimeline > uint.MaxValue)
            {
                estimatedServerTick = 0;
                return WorldTimeSyncError.TickOverflow;
            }
            estimatedServerTick = checked((uint)Math.Floor(observerTimeline));
            return WorldTimeSyncError.None;
        }
        if (ReadyConfiguration is null)
        {
            estimatedServerTick = 0;
            return WorldTimeSyncError.NoValidSample;
        }

        return timeSyncTracker.EstimateServerTick(
            nowSeconds,
            ReadyConfiguration.TickRateHz,
            out estimatedServerTick);
    }

    internal WorldTimeSyncError EstimateServerTimeline(
        double nowSeconds,
        out double estimatedServerTimeline)
    {
        if (Mode == RemoteGameplaySessionMode.Observer)
        {
            return EstimateObserverTimeline(nowSeconds, out estimatedServerTimeline);
        }
        if (ReadyConfiguration is null)
        {
            estimatedServerTimeline = 0.0;
            return WorldTimeSyncError.NoValidSample;
        }

        return timeSyncTracker.EstimateServerTimeline(
            nowSeconds,
            ReadyConfiguration.TickRateHz,
            out estimatedServerTimeline);
    }

    // Client 로컬 시간 기반 Server Timeline 계산
    internal bool TrySampleRemoteReplica(
        ClientWorldEntityKey key,
        double nowSeconds,
        out RemoteSnapshotSample sample)
    {
        sample = default;
        if (State != RemoteGameplaySessionState.Active ||
            remoteReplicas is null ||
            EstimateServerTimeline(
                nowSeconds,
                out double estimatedServerTimeline) != WorldTimeSyncError.None)
        {
            return false;
        }

        return remoteReplicas.Sample(
            key,
            estimatedServerTimeline,
            out sample) == RemoteReplicaStoreError.None;
    }

    internal bool TryBuildRemotePresentation(
        double nowSeconds,
        out IReadOnlyList<RemoteReplicaPresentationSnapshot> snapshots)
    {
        snapshots = Array.Empty<RemoteReplicaPresentationSnapshot>();
        if (State != RemoteGameplaySessionState.Active ||
            remoteReplicas is null ||
            EstimateServerTimeline(
                nowSeconds,
                out double estimatedServerTimeline) != WorldTimeSyncError.None)
        {
            return false;
        }

        return remoteReplicas.BuildPresentationSnapshots(
            estimatedServerTimeline,
            out snapshots) == RemoteReplicaStoreError.None;
    }

    internal IReadOnlyList<LeaderboardEntry> BuildLeaderboard()
    {
        if (LatestRoundResult is not null)
        {
            return roundResultLeaderboard;
        }

        return BuildActiveLeaderboard();
    }

    private IReadOnlyList<LeaderboardEntry> BuildActiveLeaderboard()
    {
        if (LatestWorldOverview is not null)
        {
            List<LeaderboardEntry> entries = new List<LeaderboardEntry>(LatestWorldOverview.Leaderboard.Count);
            for (int index = 0; index < LatestWorldOverview.Leaderboard.Count; ++index)
            {
                WorldOverviewLeaderboardEntryV3 entry = LatestWorldOverview.Leaderboard[index];
                entries.Add(new LeaderboardEntry(
                    entry.Rank,
                    entry.PlayerId,
                    entry.GrowthPoint,
                    LatestWorldOverview.ServerTick,
                    entry.DisplayName));
            }
            return entries.AsReadOnly();
        }
        if (UsesControlProtocolV2)
        {
            return Array.Empty<LeaderboardEntry>();
        }
        return gameplayState.BuildLeaderboard();
    }

    internal bool TryGetPlayerLabel(ClientWorldEntityKey key, out string label)
    {
        label = string.Empty;
        if (!playerIdentitySpawns.TryGetValue((key.EntityId, key.Generation), out EntitySpawnV2? spawn))
        {
            return false;
        }
        label = string.IsNullOrEmpty(spawn.DisplayName)
            ? $"PLAYER {spawn.PlayerId}"
            : spawn.DisplayName;
        return true;
    }

    internal bool TryProjectRound(
        double nowSeconds,
        out RoundPresentationState presentation)
    {
        presentation = default;
        uint tickRateHz = ReadyConfiguration?.TickRateHz ??
            ObserverReadyConfiguration?.TickRateHz ?? 0;
        if (tickRateHz == 0 ||
            EstimateServerTimeline(
                nowSeconds,
                out double estimatedServerTimeline) != WorldTimeSyncError.None)
        {
            return false;
        }

        return gameplayState.ProjectRound(
            estimatedServerTimeline,
            tickRateHz,
            out presentation) == AuthoritativeGameplayStateError.None;
    }

    internal RemoteGameplaySessionOperationResult AdvanceActiveFrame(
        double nowSeconds,
        float deltaSeconds,
        float inputX,
        float inputY)
    {
        if (disposed)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.Disposed,
                null);
        }
        if (State != RemoteGameplaySessionState.Active ||
            ReadyConfiguration is null ||
            IsControlledSpawnPending ||
            controlledPrediction is null)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.InvalidState,
                null);
        }
        if (!IsValidClock(nowSeconds) ||
            !float.IsFinite(deltaSeconds) ||
            deltaSeconds < 0.0f ||
            !MovementSendScheduler.TryNormalizeAndEncode(
                inputX,
                inputY,
                out Vector2 normalizedInput,
                out short encodedX,
                out short encodedY))
        {
            Fault(
                RemoteGameplaySessionFaultKind.Prediction,
                null,
                "Active frame time and movement input must be finite and monotonic.",
                requestDisconnect: true);
            return SessionFaultResult();
        }
        lastDrainTimeSeconds = nowSeconds;

        ControlledEntityPredictionError predictionError =
            controlledPrediction.SetInput(normalizedInput.X, normalizedInput.Y);
        if (predictionError == ControlledEntityPredictionError.None)
        {
            predictionError =
                controlledPrediction.AdvanceRenderCorrection(deltaSeconds);
        }
        if (predictionError != ControlledEntityPredictionError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Prediction,
                null,
                $"Could not update controlled prediction: {predictionError}.",
                requestDisconnect: true);
            return SessionFaultResult();
        }

        if (!movementScheduler.TryConsumeDeadline(nowSeconds))
        {
            return SuccessOperationResult();
        }

        WorldTimeSyncError syncError = EstimateServerTick(
            nowSeconds,
            out uint targetServerTick);
        if (syncError != WorldTimeSyncError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.TimeSync,
                null,
                $"Could not estimate movement target tick: {syncError}.",
                requestDisconnect: true);
            return SessionFaultResult();
        }

        predictionError = controlledPrediction.PredictFixedTick(staticObstacles);
        if (predictionError != ControlledEntityPredictionError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Prediction,
                null,
                $"Could not advance controlled prediction: {predictionError}.",
                requestDisconnect: true);
            return SessionFaultResult();
        }

        MovementInput movement = new MovementInput(
            ReadyConfiguration.ControlledEntityGeneration,
            targetServerTick,
            encodedX,
            encodedY);
        if (!SendPacket(movement))
        {
            return SessionFaultResult();
        }

        return SuccessOperationResult();
    }

    internal RemoteGameplaySessionOperationResult UpdateControlState(
        bool leftHeld,
        bool rightHeld,
        bool boostHeld)
    {
        if (disposed)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.Disposed,
                null);
        }
        if (State != RemoteGameplaySessionState.Active ||
            ReadyConfiguration is null ||
            IsControlledSpawnPending ||
            controlledPrediction is null ||
            !UsesControlProtocolV2)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.InvalidState,
                null);
        }

        ControlStateUpdateResult updateResult = controlStateTracker.TryCreateCommand(
            ReadyConfiguration.ControlledEntityGeneration,
            leftHeld,
            rightHeld,
            boostHeld,
            out ControlStateCommandV2 command);
        if (updateResult == ControlStateUpdateResult.Unchanged)
        {
            return SuccessOperationResult();
        }
        if (updateResult != ControlStateUpdateResult.CommandCreated)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Prediction,
                ControlStateCommandV2.PacketTypeValue,
                $"Could not create control state command: {updateResult}.",
                requestDisconnect: true);
            return SessionFaultResult();
        }
        if (!SendPacket(command))
        {
            return SessionFaultResult();
        }

        return SuccessOperationResult();
    }

    internal RemoteGameplaySessionOperationResult AdvanceActiveControlFrame(
        double nowSeconds,
        float deltaSeconds,
        bool leftHeld,
        bool rightHeld,
        bool boostHeld)
    {
        if (disposed)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.Disposed,
                null);
        }
        if (State != RemoteGameplaySessionState.Active ||
            IsControlledSpawnPending ||
            controlledPrediction is null ||
            !UsesControlProtocolV2)
        {
            return new RemoteGameplaySessionOperationResult(
                RemoteGameplaySessionOperationError.InvalidState,
                null);
        }
        if (!IsValidClock(nowSeconds) || !float.IsFinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Prediction,
                null,
                "Active control frame time must be finite and monotonic.",
                requestDisconnect: true);
            return SessionFaultResult();
        }
        lastDrainTimeSeconds = nowSeconds;

        uint growthPoint = LatestControlledStateV2?.GrowthPoint ?? 0;
        bool admittedBoostHeld = boostInputGate.Resolve(boostHeld, growthPoint);
        RemoteGameplaySessionOperationResult updateResult =
            UpdateControlState(leftHeld, rightHeld, admittedBoostHeld);
        if (!updateResult.Succeeded)
        {
            return updateResult;
        }

        ControlledEntityPredictionError predictionError = controlledPrediction.SetControlTurnState(
            ControlStateTracker.ResolveTurnState(leftHeld, rightHeld));
        if (predictionError == ControlledEntityPredictionError.None)
        {
            predictionError = controlledPrediction.AdvanceRenderCorrection(deltaSeconds);
        }
        if (predictionError == ControlledEntityPredictionError.None)
        {
            predictionError = controlledPrediction.PredictControlFrame(deltaSeconds);
        }
        if (predictionError != ControlledEntityPredictionError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Prediction,
                null,
                $"Could not advance controlled V2 prediction: {predictionError}.",
                requestDisconnect: true);
            return SessionFaultResult();
        }

        return SuccessOperationResult();
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        transport.Shutdown();
        transport.Dispose();
        disposed = true;
    }

    private bool IsValidClock(double nowSeconds)
    {
        return double.IsFinite(nowSeconds) &&
            nowSeconds >= 0.0 &&
            (!lastDrainTimeSeconds.HasValue || nowSeconds >= lastDrainTimeSeconds.Value);
    }

    private void ProcessTransportEvent(
        RemoteGameplayTransportEvent transportEvent,
        double nowSeconds)
    {
        if (transportEvent.Kind == RemoteGameplayTransportEventKind.TransportDisconnected)
        {
            bool wasLocalDisconnectRequested = localDisconnectRequested;
            ResetGenerationLocalState();
            if (!wasLocalDisconnectRequested &&
                LastFault is null &&
                LatestRoundResult is null)
            {
                string reason = transportEvent.DisconnectReason?.ToString() ?? "Unknown";
                string status = transportEvent.TransportStatus.HasValue
                    ? $"{transportEvent.TransportStatus.Value.ErrorCode} " +
                        $"({transportEvent.TransportStatus.Value.NativeErrorCode})"
                    : "missing transport status";
                LastFault = new RemoteGameplaySessionFault(
                    RemoteGameplaySessionFaultKind.TransportFailure,
                    transportGeneration,
                    null,
                    $"Transport disconnected with {reason}: {status}.");
            }
            State = RemoteGameplaySessionState.Idle;
            return;
        }

        if (State == RemoteGameplaySessionState.Faulted ||
            State == RemoteGameplaySessionState.Disconnecting)
        {
            return;
        }

        switch (transportEvent.Kind)
        {
            case RemoteGameplayTransportEventKind.TransportConnected:
                ProcessTransportConnected();
                return;
            case RemoteGameplayTransportEventKind.TransportConnectionFailed:
                ProcessTransportConnectionFailed(transportEvent);
                return;
            case RemoteGameplayTransportEventKind.PacketReceived:
                ProcessPacket(transportEvent, nowSeconds);
                return;
            default:
                Fault(
                    RemoteGameplaySessionFaultKind.TransportFailure,
                    null,
                    $"Unsupported transport event kind {transportEvent.Kind}.",
                    requestDisconnect: true);
                return;
        }
    }

    private void ProcessTransportConnected()
    {
        if (State != RemoteGameplaySessionState.Connecting)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                null,
                $"TransportConnected is invalid while the session is {State}.",
                requestDisconnect: true);
            return;
        }

        bool sent = Mode == RemoteGameplaySessionMode.Observer
            ? SendPacket(new ObserveWorldRequest())
            : SendPacket(new JoinWorldRequestV2(requestedDisplayName));
        if (!sent)
        {
            return;
        }

        State = RemoteGameplaySessionState.AwaitingBaseline;
    }

    private void ProcessTransportConnectionFailed(RemoteGameplayTransportEvent transportEvent)
    {
        if (State != RemoteGameplaySessionState.Connecting)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                null,
                $"TransportConnectionFailed is invalid while the session is {State}.",
                requestDisconnect: true);
            return;
        }

        RemoteGameplayTransportStatus? status = transportEvent.TransportStatus;
        LastFault = new RemoteGameplaySessionFault(
            RemoteGameplaySessionFaultKind.TransportFailure,
            transportGeneration,
            null,
            status.HasValue
                ? $"Connection failed with {status.Value.ErrorCode} ({status.Value.NativeErrorCode})."
                : "Connection failed without transport status.");
        ResetGenerationLocalState();
        State = RemoteGameplaySessionState.Idle;
    }

    private void ProcessPacket(
        RemoteGameplayTransportEvent transportEvent,
        double nowSeconds)
    {
        if (!transportEvent.PacketType.HasValue)
        {
            Fault(
                RemoteGameplaySessionFaultKind.ProtocolDecode,
                null,
                "PacketReceived is missing its packet type.",
                requestDisconnect: true);
            return;
        }

        uint packetType = transportEvent.PacketType.Value;
        GameplayProtocolError decodeError = ServerGameplayPacketDecoder.Decode(
            packetType,
            transportEvent.Payload.Span,
            out ServerGameplayPacket? packet);
        if (decodeError != GameplayProtocolError.Success || packet is null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.ProtocolDecode,
                packetType,
                $"Packet decode failed with {decodeError}.",
                requestDisconnect: true);
            return;
        }

        switch (State)
        {
            case RemoteGameplaySessionState.AwaitingBaseline:
                ProcessBaselinePacket(packet, nowSeconds);
                return;
            case RemoteGameplaySessionState.AwaitingFirstTimeSync:
                ProcessFirstTimeSyncPacket(packet, nowSeconds);
                return;
            case RemoteGameplaySessionState.Active:
                ProcessActivePacket(packet, nowSeconds);
                return;
            default:
                Fault(
                    RemoteGameplaySessionFaultKind.PacketOrdering,
                    packetType,
                    $"Gameplay packet {packet.GetType().Name} is invalid while the session is {State}.",
                    requestDisconnect: true);
                return;
        }
    }

    private void ProcessBaselinePacket(ServerGameplayPacket packet, double nowSeconds)
    {
        if (Mode == RemoteGameplaySessionMode.Observer)
        {
            if (packet is RoundState observerRound)
            {
                ApplyGameplayRound(observerRound);
                return;
            }
            if (packet is ObserverReady observerReady)
            {
                ProcessObserverReady(observerReady, nowSeconds);
                return;
            }

            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                packet.PacketType,
                $"{packet.GetType().Name} arrived during observer admission.",
                requestDisconnect: true);
            return;
        }

        switch (packet)
        {
            case EntitySpawnV2 spawn:
                baselineSpawns[(spawn.Baseline.EntityId, spawn.Baseline.Generation)] = spawn.Baseline;
                if (spawn.Baseline.EntityKind == EntityKind.Player)
                {
                    playerIdentitySpawns[(spawn.Baseline.EntityId, spawn.Baseline.Generation)] = spawn;
                }
                return;
            case EntitySpawn spawn:
                baselineSpawns[(spawn.EntityId, spawn.Generation)] = spawn;
                return;
            case ScoreState score:
                ApplyGameplayScore(score);
                return;
            case RoundState round:
                ApplyGameplayRound(round);
                return;
            case WorldReady ready:
                ProcessWorldReady(ready, nowSeconds);
                return;
            case WorldReadyV2 ready:
                ProcessWorldReadyV2(ready, nowSeconds);
                return;
            default:
                Fault(
                    RemoteGameplaySessionFaultKind.PacketOrdering,
                    packet.PacketType,
                    $"{packet.GetType().Name} arrived before WorldReady.",
                    requestDisconnect: true);
                return;
        }
    }

    private void ProcessWorldReady(WorldReady ready, double nowSeconds)
    {
        if (ReadyConfiguration is not null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                ready.PacketType,
                "WorldReady may only be accepted once per transport generation.",
                requestDisconnect: true);
            return;
        }

        (uint EntityId, uint Generation) controlledKey =
            (ready.ControlledEntityId, ready.ControlledEntityGeneration);
        if (!baselineSpawns.TryGetValue(controlledKey, out EntitySpawn? controlledSpawn) ||
            controlledSpawn.EntityKind != EntityKind.Player)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                ready.PacketType,
                "WorldReady does not match a staged Player EntitySpawn.",
                requestDisconnect: true);
            return;
        }

        ReadyConfiguration = ready;
        ulong doubledSnapshotInterval =
            (ulong)ready.SnapshotIntervalTicks * 2UL;
        if (doubledSnapshotInterval > uint.MaxValue)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Replication,
                ready.PacketType,
                "Snapshot interval exceeds the client replica timeline range.",
                requestDisconnect: true);
            return;
        }

        remoteReplicas = new RemoteReplicaStore(
            ready.TickRateHz,
            ready.SnapshotIntervalTicks);
        staticObstacles.Clear();
        foreach (EntitySpawn spawn in baselineSpawns.Values)
        {
            if (spawn.EntityId != ready.ControlledEntityId ||
                spawn.Generation != ready.ControlledEntityGeneration)
            {
                RemoteReplicaStoreError replicaError =
                    remoteReplicas.ApplySpawn(
                        spawn,
                        out RemoteReplicaSpawnOutcome _);
                if (replicaError != RemoteReplicaStoreError.None)
                {
                    Fault(
                        RemoteGameplaySessionFaultKind.Replication,
                        spawn.PacketType,
                        $"Could not stage baseline replica: {replicaError}.",
                        requestDisconnect: true);
                    return;
                }
            }
            if (spawn.EntityKind == EntityKind.StaticObstacle)
            {
                UpsertStaticObstacle(spawn);
            }
        }
        controlledPrediction = new ControlledEntityPrediction(
            controlledSpawn,
            ready,
            physics: predictionPhysics);
        if (!BeginAndSendTimeSyncProbe(nowSeconds))
        {
            return;
        }

        State = RemoteGameplaySessionState.AwaitingFirstTimeSync;
    }

    private void ProcessObserverReady(ObserverReady ready, double nowSeconds)
    {
        if (Mode != RemoteGameplaySessionMode.Observer ||
            ObserverReadyConfiguration is not null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                ready.PacketType,
                "ObserverReady requires one pending observer admission.",
                requestDisconnect: true);
            return;
        }
        if (expectedChannelId != 0 && ready.ChannelId != expectedChannelId)
        {
            Fault(
                RemoteGameplaySessionFaultKind.GameplayState,
                ready.PacketType,
                $"ObserverReady channel mismatch. expected={expectedChannelId} actual={ready.ChannelId}.",
                requestDisconnect: true);
            return;
        }

        ObserverReadyConfiguration = ready;
        observerReadyReceivedAtSeconds = nowSeconds;
        ChannelId = ready.ChannelId;
        State = RemoteGameplaySessionState.Active;
    }

    private void ProcessWorldReadyV2(WorldReadyV2 ready, double nowSeconds)
    {
        if (expectedChannelId != 0 && ready.ChannelId != expectedChannelId)
        {
            Fault(
                RemoteGameplaySessionFaultKind.GameplayState,
                WorldReadyV2.PacketTypeValue,
                $"WorldReady channel mismatch. expected={expectedChannelId} actual={ready.ChannelId}.",
                requestDisconnect: true);
            return;
        }

        WorldReady gameplayReady = new WorldReady(
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
            ready.ArenaMaxY);
        ProcessWorldReady(gameplayReady, nowSeconds);
        if (State == RemoteGameplaySessionState.AwaitingFirstTimeSync && ReadyConfiguration == gameplayReady)
        {
            ChannelId = ready.ChannelId;
            DisplayName = ready.DisplayName;
        }
    }

    private void ProcessFirstTimeSyncPacket(
        ServerGameplayPacket packet,
        double nowSeconds)
    {
        if (packet is not WorldTimeSyncResponse response)
        {
            // WorldReady closes the baseline. Authoritative state publication can
            // legitimately overtake the time-sync response, while movement remains gated.
            ProcessActivePacket(packet, nowSeconds);
            return;
        }

        if (!AcceptTimeSyncResponse(response, nowSeconds))
        {
            return;
        }

        movementScheduler.Start(nowSeconds, ReadyConfiguration!.TickRateHz);
        State = RemoteGameplaySessionState.Active;
    }

    private void ProcessActivePacket(ServerGameplayPacket packet, double nowSeconds)
    {
        if (Mode == RemoteGameplaySessionMode.Observer)
        {
            ProcessObserverActivePacket(packet);
            return;
        }
        if (packet is WorldTimeSyncResponse response)
        {
            AcceptTimeSyncResponse(response, nowSeconds);
            return;
        }
        if (packet is ControlledEntityState controlledState)
        {
            if (controlledPrediction is null)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.PacketOrdering,
                    packet.PacketType,
                    "ControlledEntityState requires initialized controlled prediction.",
                    requestDisconnect: true);
                return;
            }

            ControlledEntityPredictionError predictionError =
                controlledPrediction.ApplyAuthoritative(controlledState);
            if (predictionError == ControlledEntityPredictionError.StaleGeneration)
            {
                return;
            }
            if (predictionError != ControlledEntityPredictionError.None)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.Prediction,
                    packet.PacketType,
                    $"Controlled authoritative state was rejected: {predictionError}.",
                    requestDisconnect: true);
            }
            return;
        }
        if (packet is EntitySpawnV2 spawnV2)
        {
            if (spawnV2.Baseline.EntityKind == EntityKind.Player)
            {
                playerIdentitySpawns[(spawnV2.Baseline.EntityId, spawnV2.Baseline.Generation)] = spawnV2;
            }
            ApplyActiveReplicaSpawn(spawnV2.Baseline);
            return;
        }
        if (packet is EntitySpawn spawn)
        {
            ApplyActiveReplicaSpawn(spawn);
            return;
        }
        if (packet is ControlledEntityStateV2 controlledStateV2)
        {
            if (controlledPrediction is null)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.PacketOrdering,
                    packet.PacketType,
                    "ControlledEntityState V2 requires initialized controlled prediction.",
                    requestDisconnect: true);
                return;
            }

            if (controlledStateV2.ControlledEntityGeneration != controlledPrediction.Generation)
            {
                return;
            }
            ControlAcknowledgementResult acknowledgementResult =
                controlStateTracker.AcceptAcknowledgement(controlledStateV2.LastProcessedControlSequence);
            if (acknowledgementResult == ControlAcknowledgementResult.Stale)
            {
                return;
            }
            if (acknowledgementResult != ControlAcknowledgementResult.Accepted)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.ProtocolDecode,
                    packet.PacketType,
                    $"Controlled authoritative acknowledgement was rejected: {acknowledgementResult}.",
                    requestDisconnect: true);
                return;
            }

            ControlledEntityPredictionError predictionError =
                controlledPrediction.ApplyAuthoritative(controlledStateV2);
            if (predictionError == ControlledEntityPredictionError.StaleGeneration)
            {
                return;
            }
            if (predictionError != ControlledEntityPredictionError.None)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.Prediction,
                    packet.PacketType,
                    $"Controlled authoritative V2 state was rejected: {predictionError}.",
                    requestDisconnect: true);
                return;
            }
            LatestControlledStateV2 = controlledStateV2;
            UsesControlProtocolV2 = true;
            return;
        }
        if (packet is ControlledEntityRebind rebind)
        {
            ApplyControlledEntityRebind(rebind);
            return;
        }
        if (packet is WorldOverviewSnapshotV3 overviewChunk)
        {
            ChunkGroupAcceptResult overviewResult =
                worldOverviewAssembler.Accept(overviewChunk, out WorldOverviewState? committedOverview);
            if (overviewResult == ChunkGroupAcceptResult.Committed)
            {
                LatestWorldOverview = committedOverview;    // chunk가 완성된 경우에만 교체
            }
            return;
        }
        if (packet is EntityStateBatchV2 stateChunk)
        {
            ApplyActiveReplicaStateChunk(stateChunk);
            return;
        }
        if (packet is EntityStateBatch batch)
        {
            ApplyActiveReplicaStateBatch(batch);
            return;
        }
        if (packet is EntityRemove remove)
        {
            ApplyActiveReplicaRemove(remove);
            return;
        }
        if (packet is ScoreState score)
        {
            ApplyGameplayScore(score);
            return;
        }
        if (packet is RoundState round)
        {
            ApplyGameplayRound(round);
            return;
        }
        if (packet is RoundResultV2 roundResult)
        {
            CaptureRoundResultLeaderboard();
            LatestRoundResultRecipientPlayerId = ReadyConfiguration?.PlayerId;
            LatestRoundResult = roundResult;
            _ = Disconnect();
            return;
        }

        Fault(
            RemoteGameplaySessionFaultKind.PacketOrdering,
            packet.PacketType,
            $"{packet.GetType().Name} requires a later gameplay slice.",
            requestDisconnect: true);
    }

    private void ProcessObserverActivePacket(ServerGameplayPacket packet)
    {
        if (packet is WorldOverviewSnapshotV3 overviewChunk)
        {
            ChunkGroupAcceptResult overviewResult =
                worldOverviewAssembler.Accept(overviewChunk, out WorldOverviewState? committedOverview);
            if (overviewResult == ChunkGroupAcceptResult.Committed)
            {
                LatestWorldOverview = committedOverview;
            }
            return;
        }
        if (packet is RoundState round)
        {
            ApplyGameplayRound(round);
            return;
        }
        if (packet is RoundResultV2 roundResult)
        {
            CaptureRoundResultLeaderboard();
            LatestRoundResultRecipientPlayerId = null;
            LatestRoundResult = roundResult;
            _ = Disconnect();
            return;
        }

        Fault(
            RemoteGameplaySessionFaultKind.PacketOrdering,
            packet.PacketType,
            $"{packet.GetType().Name} is not valid for an observer session.",
            requestDisconnect: true);
    }

    private WorldTimeSyncError EstimateObserverTimeline(
        double nowSeconds,
        out double estimatedServerTimeline)
    {
        estimatedServerTimeline = 0.0;
        if (ObserverReadyConfiguration is null ||
            !observerReadyReceivedAtSeconds.HasValue)
        {
            return WorldTimeSyncError.NoValidSample;
        }
        if (!double.IsFinite(nowSeconds) ||
            nowSeconds < observerReadyReceivedAtSeconds.Value)
        {
            return WorldTimeSyncError.InvalidTime;
        }

        estimatedServerTimeline = ObserverReadyConfiguration.CurrentServerTick +
            ((nowSeconds - observerReadyReceivedAtSeconds.Value) *
                ObserverReadyConfiguration.TickRateHz);
        return double.IsFinite(estimatedServerTimeline)
            ? WorldTimeSyncError.None
            : WorldTimeSyncError.TickOverflow;
    }

    private bool BeginAndSendTimeSyncProbe(double nowSeconds)
    {
        WorldTimeSyncError syncError = timeSyncTracker.BeginProbe(
            nowSeconds,
            out WorldTimeSyncRequest request);
        if (syncError != WorldTimeSyncError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.TimeSync,
                null,
                $"Could not begin a time-sync probe: {syncError}.",
                requestDisconnect: true);
            return false;
        }

        return SendPacket(request);
    }

    private bool AcceptTimeSyncResponse(
        WorldTimeSyncResponse response,
        double nowSeconds)
    {
        if (ReadyConfiguration is null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                response.PacketType,
                "A time-sync response requires WorldReady configuration.",
                requestDisconnect: true);
            return false;
        }

        WorldTimeSyncError syncError = timeSyncTracker.AcceptResponse(
            response,
            nowSeconds,
            ReadyConfiguration.TickRateHz);
        if (syncError != WorldTimeSyncError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.TimeSync,
                response.PacketType,
                $"Time-sync response was rejected: {syncError}.",
                requestDisconnect: true);
            return false;
        }

        syncError = timeSyncTracker.EstimateServerTick(
            nowSeconds,
            ReadyConfiguration.TickRateHz,
            out uint _);
        if (syncError != WorldTimeSyncError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.TimeSync,
                response.PacketType,
                $"Time-sync tick estimate was rejected: {syncError}.",
                requestDisconnect: true);
            return false;
        }

        return true;
    }

    private bool SendPacket<TPacket>(TPacket packet)
        where TPacket : struct, IClientGameplayPacket
    {
        Span<byte> payload = stackalloc byte[packet.PayloadByteCount];
        GameplayProtocolError encodeError = ClientGameplayPacketEncoder.Encode(
            packet,
            payload,
            out EncodedClientGameplayPacket encoded);
        if (encodeError != GameplayProtocolError.Success)
        {
            Fault(
                RemoteGameplaySessionFaultKind.ProtocolDecode,
                packet.PacketType,
                $"Client packet encode failed with {encodeError}.",
                requestDisconnect: true);
            return false;
        }

        RemoteGameplayTransportStatus status = transport.Send(
            encoded.PacketType,
            payload[..encoded.PayloadByteCount]);
        if (!status.Succeeded)
        {
            Fault(
                RemoteGameplaySessionFaultKind.SendFailure,
                encoded.PacketType,
                $"Send failed with {status.ErrorCode} ({status.NativeErrorCode}).",
                requestDisconnect: true);
            return false;
        }

        return true;
    }

    private void Fault(
        RemoteGameplaySessionFaultKind kind,
        uint? packetType,
        string message,
        bool requestDisconnect)
    {
        LastFault = new RemoteGameplaySessionFault(
            kind,
            transportGeneration,
            packetType,
            message);
        State = RemoteGameplaySessionState.Faulted;
        if (requestDisconnect)
        {
            transport.Disconnect();
        }
    }

    private void ResetGenerationLocalState()
    {
        baselineSpawns.Clear();
        playerIdentitySpawns.Clear();
        gameplayState.Clear();
        ReadyConfiguration = null;
        ObserverReadyConfiguration = null;
        Mode = RemoteGameplaySessionMode.Player;
        ChannelId = 0;
        DisplayName = string.Empty;
        requestedDisplayName = string.Empty;
        expectedChannelId = 0;
        observerReadyReceivedAtSeconds = null;
        localDisconnectRequested = false;
        timeSyncTracker.Reset();
        movementScheduler.Reset();
        controlStateTracker.Reset();
        boostInputGate.Reset();
        worldOverviewAssembler.Clear();
        remoteEntityStateAssembler.Clear();
        staticObstacles.Clear();
        IsControlledSpawnPending = false;
        controlledPrediction = null;
        LatestControlledStateV2 = null;
        LatestWorldOverview = null;
        UsesControlProtocolV2 = false;
        remoteReplicas = null;
        LastReplicaAnomaly = null;
        lastDrainTimeSeconds = null;
    }

    private void CaptureRoundResultLeaderboard()
    {
        IReadOnlyList<LeaderboardEntry> activeLeaderboard = BuildActiveLeaderboard();
        if (activeLeaderboard.Count == 0)
        {
            roundResultLeaderboard = Array.Empty<LeaderboardEntry>();
            return;
        }

        List<LeaderboardEntry> snapshot = new List<LeaderboardEntry>(activeLeaderboard.Count);
        for (int index = 0; index < activeLeaderboard.Count; ++index)
        {
            snapshot.Add(activeLeaderboard[index]);
        }
        roundResultLeaderboard = snapshot.AsReadOnly();
    }

    private void ApplyGameplayScore(ScoreState score)
    {
        AuthoritativeGameplayStateError gameplayError =
            gameplayState.ApplyScore(score);
        if (gameplayError != AuthoritativeGameplayStateError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.GameplayState,
                score.PacketType,
                $"Authoritative score state was rejected: {gameplayError}.",
                requestDisconnect: true);
        }
    }

    private void ApplyGameplayRound(RoundState round)
    {
        AuthoritativeGameplayStateError gameplayError =
            gameplayState.ApplyRound(round);
        if (gameplayError != AuthoritativeGameplayStateError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.GameplayState,
                round.PacketType,
                $"Authoritative round state was rejected: {gameplayError}.",
                requestDisconnect: true);
        }
    }

    private void ApplyActiveReplicaSpawn(EntitySpawn spawn)
    {
        if (ReadyConfiguration is null || remoteReplicas is null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                spawn.PacketType,
                "EntitySpawn requires initialized replica state.",
                requestDisconnect: true);
            return;
        }
        if (spawn.EntityId == ReadyConfiguration.ControlledEntityId &&
            spawn.Generation == ReadyConfiguration.ControlledEntityGeneration)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                spawn.PacketType,
                "The controlled entity cannot be added to the remote replica store.",
                requestDisconnect: true);
            return;
        }

        RemoteReplicaStoreError replicaError = remoteReplicas.ApplySpawn(
            spawn,
            out RemoteReplicaSpawnOutcome outcome);
        if (replicaError != RemoteReplicaStoreError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Replication,
                spawn.PacketType,
                $"Remote replica spawn was rejected: {replicaError}.",
                requestDisconnect: true);
            return;
        }

        if (outcome == RemoteReplicaSpawnOutcome.ReplacedGeneration)
        {
            LastReplicaAnomaly =
                $"Entity {spawn.EntityId} replaced a replica generation without an earlier remove.";
        }
        RemoveStaticObstaclesByEntityId(spawn.EntityId);
        if (spawn.EntityKind == EntityKind.StaticObstacle)
        {
            UpsertStaticObstacle(spawn);
        }
    }

    private void ApplyActiveReplicaStateBatch(EntityStateBatch batch)
    {
        if (remoteReplicas is null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                batch.PacketType,
                "EntityStateBatch requires initialized replica state.",
                requestDisconnect: true);
            return;
        }

        RemoteReplicaStoreError replicaError =
            remoteReplicas.ApplyStateBatch(batch);
        if (replicaError != RemoteReplicaStoreError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Replication,
                batch.PacketType,
                $"Remote replica state batch was rejected: {replicaError}.",
                requestDisconnect: true);
        }
    }

    private void ApplyActiveReplicaStateChunk(EntityStateBatchV2 chunk)
    {
        if (remoteReplicas is null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                chunk.PacketType,
                "EntityStateBatch V2 requires initialized replica state.",
                requestDisconnect: true);
            return;
        }

        ChunkGroupAcceptResult acceptResult =
            remoteEntityStateAssembler.Accept(chunk, out RemoteEntityStateGroup? group);
        if (acceptResult == ChunkGroupAcceptResult.InvalidGroup)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Replication,
                chunk.PacketType,
                "Remote entity state group was rejected by the assembler.",
                requestDisconnect: true);
            return;
        }
        if (acceptResult != ChunkGroupAcceptResult.Committed)
        {
            return;
        }

        RemoteReplicaStoreError replicaError = remoteReplicas.ApplyStateGroup(group!);
        if (replicaError != RemoteReplicaStoreError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Replication,
                chunk.PacketType,
                $"Remote whole-body state group was rejected: {replicaError}.",
                requestDisconnect: true);
        }
    }

    private void ApplyControlledEntityRebind(ControlledEntityRebind rebind)
    {
        if (ReadyConfiguration is null || remoteReplicas is null ||
            !IsControlledSpawnPending || controlledPrediction is not null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                rebind.PacketType,
                "ControlledEntityRebind requires an initialized SpawnPending state.",
                requestDisconnect: true);
            return;
        }
        if (rebind.PlayerId != ReadyConfiguration.PlayerId ||
            rebind.PreviousEntityId != ReadyConfiguration.ControlledEntityId ||
            rebind.PreviousEntityGeneration != ReadyConfiguration.ControlledEntityGeneration)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                rebind.PacketType,
                "ControlledEntityRebind does not match the current player and controlled entity.",
                requestDisconnect: true);
            return;
        }

        ClientWorldEntityKey controlledKey = new ClientWorldEntityKey(
            rebind.ControlledEntityId,
            rebind.ControlledEntityGeneration);
        RemoteReplicaStoreError replicaError = remoteReplicas.TryTakeForControl(
            controlledKey,
            out EntitySpawn? controlledSpawn);
        if (replicaError != RemoteReplicaStoreError.None ||
            controlledSpawn is null ||
            controlledSpawn.EntityKind != EntityKind.Player ||
            controlledSpawn.ServerTick != rebind.ServerTick)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                rebind.PacketType,
                "ControlledEntityRebind requires the matching Player EntitySpawn from the same server tick.",
                requestDisconnect: true);
            return;
        }

        WorldReady reboundConfiguration = ReadyConfiguration with
        {
            ControlledEntityId = rebind.ControlledEntityId,
            ControlledEntityGeneration = rebind.ControlledEntityGeneration,
        };
        controlledPrediction = new ControlledEntityPrediction(
            controlledSpawn,
            reboundConfiguration,
            physics: predictionPhysics);
        LatestControlledStateV2 = null;
        controlStateTracker.Reset();
        boostInputGate.Reset();
        ReadyConfiguration = reboundConfiguration;
        IsControlledSpawnPending = false;
    }

    private void ApplyActiveReplicaRemove(EntityRemove remove)
    {
        playerIdentitySpawns.Remove((remove.EntityId, remove.Generation));
        if (ReadyConfiguration is not null &&
            remove.EntityId == ReadyConfiguration.ControlledEntityId &&
            remove.Generation == ReadyConfiguration.ControlledEntityGeneration)
        {
            if (remove.Reason != EntityRemoveReason.Destroyed)
            {
                Fault(
                    RemoteGameplaySessionFaultKind.GameplayState,
                    remove.PacketType,
                    $"Controlled entity removal requires Destroyed, not {remove.Reason}.",
                    requestDisconnect: true);
                return;
            }
            if (IsControlledSpawnPending)
            {
                return;
            }

            frameRemovalNotices.Add(new RemoteGameplayRemovalNotice(
                remove.ServerTick,
                new ClientWorldEntityKey(remove.EntityId, remove.Generation),
                remove.Reason));
            IsControlledSpawnPending = true;
            controlledPrediction = null;
            LatestControlledStateV2 = null;
            controlStateTracker.Reset();
            boostInputGate.Reset();
            return;
        }

        if (remoteReplicas is null)
        {
            Fault(
                RemoteGameplaySessionFaultKind.PacketOrdering,
                remove.PacketType,
                "EntityRemove requires initialized replica state.",
                requestDisconnect: true);
            return;
        }

        RemoteReplicaStoreError replicaError = remoteReplicas.ApplyRemove(
            remove,
            out RemoteReplicaRemoveOutcome outcome);
        if (replicaError != RemoteReplicaStoreError.None)
        {
            Fault(
                RemoteGameplaySessionFaultKind.Replication,
                remove.PacketType,
                $"Remote replica remove was rejected: {replicaError}.",
                requestDisconnect: true);
            return;
        }
        if (outcome == RemoteReplicaRemoveOutcome.Removed)
        {
            frameRemovalNotices.Add(new RemoteGameplayRemovalNotice(
                remove.ServerTick,
                new ClientWorldEntityKey(remove.EntityId, remove.Generation),
                remove.Reason));
            RemoveStaticObstacle(
                new ClientWorldEntityKey(remove.EntityId, remove.Generation));
        }
    }

    private void UpsertStaticObstacle(EntitySpawn spawn)
    {
        RemoveStaticObstaclesByEntityId(spawn.EntityId);
        staticObstacles.Add(new ClientStaticObstacle(
            spawn.EntityId,
            spawn.Generation,
            new Vector2(spawn.PositionX, spawn.PositionY),
            spawn.PrimaryCircleRadius));
    }

    private void RemoveStaticObstaclesByEntityId(uint entityId)
    {
        for (int index = staticObstacles.Count - 1; index >= 0; --index)
        {
            if (staticObstacles[index].EntityId == entityId)
            {
                staticObstacles.RemoveAt(index);
            }
        }
    }

    private void RemoveStaticObstacle(ClientWorldEntityKey key)
    {
        for (int index = staticObstacles.Count - 1; index >= 0; --index)
        {
            ClientStaticObstacle obstacle = staticObstacles[index];
            if (obstacle.EntityId == key.EntityId &&
                obstacle.Generation == key.Generation)
            {
                staticObstacles.RemoveAt(index);
            }
        }
    }

    private static RemoteGameplaySessionOperationResult SuccessOperationResult()
    {
        return new RemoteGameplaySessionOperationResult(
            RemoteGameplaySessionOperationError.None,
            null);
    }

    private static RemoteGameplaySessionOperationResult SessionFaultResult()
    {
        return new RemoteGameplaySessionOperationResult(
            RemoteGameplaySessionOperationError.SessionFault,
            null);
    }
}
