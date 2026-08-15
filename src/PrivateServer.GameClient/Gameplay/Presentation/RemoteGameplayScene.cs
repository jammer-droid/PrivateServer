using Godot;
using PrivateServer.GameClient.Gameplay.Flow;
using PrivateServer.GameClient.Gameplay.Prediction;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Remote;
using PrivateServer.GameClient.Gameplay.Replication;
using PrivateServer.NetworkRuntime.Managed;
using System;
using System.Collections.Generic;

using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;

namespace PrivateServer.GameClient.Gameplay.Presentation;

public partial class RemoteGameplayScene : Node2D
{
    private readonly record struct RemovalPresentationCue(
        EntityRemoveReason Reason,
        Vector2 Position,
        float RadiusPixels);

    private static readonly StringName MoveLeftAction = "move_left";
    private static readonly StringName MoveRightAction = "move_right";
    private static readonly StringName MoveUpAction = "move_up";
    private static readonly StringName MoveDownAction = "move_down";
    private static readonly StringName BoostAction = "boost";
    private static readonly StringName ReconnectAction = "reconnect";
    private static readonly StringName DeveloperOverlayAction = "developer_overlay";

    private readonly Dictionary<ClientWorldEntityKey, GameplayReplicaNode> remoteNodes = new();
    private readonly Dictionary<ClientWorldEntityKey, GameplayBodyLine> remoteBodyLines = new();
    private readonly GameplayPresentationCueProjector cueProjector = new();
    private GameplayFlow flow = null!;
    private GameplayChannelDirectory channelDirectory = null!;
    private GameplayChannelOption channel;
    private RemoteGameplaySession session = null!;
    private GameplayNavigationUi navigationUi = null!;
    private GameplayHud hud = null!;
    private DeveloperOverlay developerOverlay = null!;
    private ArenaPresentation arenaPresentation = null!;
    private WorldOverviewPresentation worldOverviewPresentation = null!;
    private GameplayEffects effects = null!;
    private GameplayAudio audio = null!;
    private Node2D worldEntities = null!;
    private Camera2D camera = null!;
    private GameplayReplicaNode? controlledNode;
    private GameplayBodyLine? controlledBodyLine;
    private ClientWorldEntityKey? controlledKey;
    private uint? presentationTransportGeneration;

    [Export]
    public PackedScene ReplicaScene { get; set; } = null!;

    [Export]
    public PackedScene BodyLineScene { get; set; } = null!;

    [Export]
    public string ChannelManifestPath { get; set; } =
        "res://Config/channels.local.json";

    [Export]
    public float PixelsPerUnit { get; set; } = 32.0f;

    public override void _Ready()
    {
        if (!TryLoadChannelDirectory() ||
            !float.IsFinite(PixelsPerUnit) ||
            PixelsPerUnit <= 0.0f ||
            ReplicaScene is null ||
            BodyLineScene is null)
        {
            GD.PushError("Remote gameplay presentation configuration is invalid.");
            SetProcess(false);
            return;
        }

        RenderingServer.SetDefaultClearColor(new Color("0d111b"));

        camera = GetNode<Camera2D>("%GameplayCamera");
        camera.Enabled = true;
        arenaPresentation = GetNode<ArenaPresentation>("%ArenaPresentation");
        worldOverviewPresentation = GetNode<WorldOverviewPresentation>("%WorldOverviewPresentation");
        worldEntities = GetNode<Node2D>("%WorldEntities");
        effects = GetNode<GameplayEffects>("%GameplayEffects");
        hud = GetNode<GameplayHud>("%GameplayHud");
        navigationUi = GetNode<GameplayNavigationUi>("%GameplayNavigationUi");
        developerOverlay = GetNode<DeveloperOverlay>("%DeveloperOverlay");
        audio = GetNode<GameplayAudio>("%GameplayAudio");

        flow = new GameplayFlow();
        session = new RemoteGameplaySession();
        navigationUi.ChannelSelected += SelectChannel;
        navigationUi.ObserveRequested += BeginObserve;
        navigationUi.ConnectRequested += BeginConnect;
        navigationUi.ReturnToChannelSelectRequested += ReturnToChannelSelect;
        navigationUi.ExitRequested += ExitApplication;
        navigationUi.ConfigureChannels(channelDirectory.Channels);
        audio.BindButtons(navigationUi);
        ApplyFlowPresentation();
        ApplyLaunchOptions();
    }

    public override void _Process(double delta)
    {
        double nowSeconds = Time.GetTicksMsec() / 1000.0;
        RemoteGameplayDrainResult drainResult = session.DrainFrame(nowSeconds);

        if (Input.IsActionJustPressed(DeveloperOverlayAction))
        {
            developerOverlay.Visible = !developerOverlay.Visible;
            audio.PlayOverlayToggle();
        }

        if (flow.State == GameplayFlowState.Result &&
            Input.IsActionJustPressed(ReconnectAction))
        {
            ReturnToChannelSelect();
        }

        if (flow.State != GameplayFlowState.ChannelSelect &&
            flow.State != GameplayFlowState.PlayerSetup &&
            flow.State != GameplayFlowState.Exiting)
        {
            flow.TryApply(GameplayFlowObservation.FromSession(session));
        }

        if (flow.State == GameplayFlowState.Playing)
        {
            RemoteGameplaySessionOperationResult frameResult;
            if (session.UsesControlProtocolV2)
            {
                frameResult = session.AdvanceActiveControlFrame(
                    nowSeconds,
                    checked((float)delta),
                    Input.IsActionPressed(MoveLeftAction),
                    Input.IsActionPressed(MoveRightAction),
                    Input.IsActionPressed(BoostAction));
            }
            else
            {
                Vector2 input = SampleServerMovementInput();
                frameResult = session.AdvanceActiveFrame(
                    nowSeconds,
                    checked((float)delta),
                    input.X,
                    input.Y);
            }
            if (!frameResult.Succeeded &&
                frameResult.Error != RemoteGameplaySessionOperationError.InvalidState)
            {
                GD.PushError($"Active gameplay frame failed: {frameResult.Error}.");
            }
        }

        List<RemovalPresentationCue> removalCues =
            CaptureRemovalCues(drainResult);
        ApplyPresentation(nowSeconds);
        ApplyStateCues();
        ApplyRemovalCues(removalCues);
        ApplyActiveAreaWarning(nowSeconds);
        hud.UpdatePresentation(session, nowSeconds, drainResult);
        developerOverlay.UpdatePresentation(session, nowSeconds, drainResult);
        ApplyFlowPresentation();
    }

    public override void _ExitTree()
    {
        session?.Dispose();
    }

    private bool TryLoadChannelDirectory()
    {
        if (!FileAccess.FileExists(ChannelManifestPath))
        {
            GD.PushError($"Channel manifest does not exist: {ChannelManifestPath}");
            return false;
        }

        string json = FileAccess.GetFileAsString(ChannelManifestPath);
        if (!GameplayChannelDirectory.TryParse(
                json,
                out GameplayChannelDirectory? loadedDirectory) ||
            loadedDirectory is null)
        {
            GD.PushError($"Channel manifest is invalid: {ChannelManifestPath}");
            return false;
        }

        channelDirectory = loadedDirectory;
        return true;
    }

    private void SelectChannel(int index)
    {
        if (index < 0 ||
            index >= channelDirectory.Channels.Count ||
            session.State != RemoteGameplaySessionState.Idle ||
            !flow.TrySelectChannel())
        {
            return;
        }

        channel = channelDirectory.Channels[index];
        navigationUi.ConfigureSelectedChannel(channel);
        hud.ConfigureChannel(channel);
        developerOverlay.ConfigureChannel(channel);
        ApplyFlowPresentation();
    }

    private void BeginConnect(string displayName)
    {
        if (session.State != RemoteGameplaySessionState.Idle ||
            !flow.TryBeginConnection() ||
            !channel.TryCreateEndpoint(out NetworkRuntimeIpv4Endpoint endpoint))
        {
            return;
        }

        RemoteGameplaySessionOperationResult result = session.Connect(
            endpoint,
            displayName,
            channel.Id);
        if (!result.Succeeded)
        {
            GD.PushError($"World server connect request failed: {result.Error}.");
            flow.TryApply(GameplayFlowObservation.FromSession(session));
        }
        ApplyFlowPresentation();
    }

    private void BeginObserve(int index)
    {
        if (index < 0 ||
            index >= channelDirectory.Channels.Count ||
            session.State != RemoteGameplaySessionState.Idle ||
            !flow.TryBeginObservation())
        {
            return;
        }

        channel = channelDirectory.Channels[index];
        hud.ConfigureChannel(channel);
        developerOverlay.ConfigureChannel(channel);
        if (!channel.TryCreateEndpoint(out NetworkRuntimeIpv4Endpoint endpoint))
        {
            return;
        }

        RemoteGameplaySessionOperationResult result = session.Connect(
            endpoint,
            expectedChannelId: channel.Id,
            mode: RemoteGameplaySessionMode.Observer);
        if (!result.Succeeded)
        {
            GD.PushError($"World observer connect request failed: {result.Error}.");
            flow.TryApply(GameplayFlowObservation.FromSession(session));
        }
        ApplyFlowPresentation();
    }

    private void ApplyLaunchOptions()
    {
        string[] arguments = OS.GetCmdlineUserArgs();
        if (!GameplayLaunchOptions.TryParse(arguments, out GameplayLaunchOptions options))
        {
            GD.PushError("Invalid game client launch arguments.");
            SetProcess(false);
            return;
        }
        if (!options.ObserverChannelId.HasValue)
        {
            return;
        }

        for (int index = 0; index < channelDirectory.Channels.Count; ++index)
        {
            if (channelDirectory.Channels[index].Id == options.ObserverChannelId.Value)
            {
                BeginObserve(index);
                return;
            }
        }

        GD.PushError(
            $"Observer channel does not exist in the client directory: {options.ObserverChannelId.Value}.");
        SetProcess(false);
    }

    private void ReturnToChannelSelect()
    {
        if (!flow.TryReturnToChannelSelect())
        {
            return;
        }
        if (flow.State == GameplayFlowState.ReturningToChannelSelect &&
            session.State != RemoteGameplaySessionState.Disconnecting)
        {
            RemoteGameplaySessionOperationResult result = session.Disconnect();
            if (!result.Succeeded &&
                result.Error != RemoteGameplaySessionOperationError.InvalidState)
            {
                GD.PushError($"World session disconnect request failed: {result.Error}.");
            }
        }
        channel = default;
        navigationUi.ResetPlayerSetup();
        ApplyFlowPresentation();
    }

    private void ExitApplication()
    {
        if (!flow.TryExit())
        {
            return;
        }

        SetProcess(false);
        session.Dispose();
        GetTree().Quit();
    }

    private void ApplyFlowPresentation()
    {
        GameplayFlowState state = flow.State;
        hud.Visible =
            state == GameplayFlowState.Playing ||
            state == GameplayFlowState.Observing ||
            state == GameplayFlowState.SpawnPending ||
            state == GameplayFlowState.Result;
        navigationUi.Apply(
            state,
            session.LastFault?.Message,
            session.State == RemoteGameplaySessionState.Idle);
        audio.ApplyFlow(state);
    }

    private Vector2 SampleServerMovementInput()
    {
        float inputX =
            Input.GetActionStrength(MoveRightAction) -
            Input.GetActionStrength(MoveLeftAction);
        float inputY =
            Input.GetActionStrength(MoveUpAction) -
            Input.GetActionStrength(MoveDownAction);
        Vector2 input = new Vector2(inputX, inputY);
        return input.LengthSquared() > 1.0f
            ? input.Normalized()
            : input;
    }

    private void ApplyPresentation(double nowSeconds)
    {
        WorldReady? ready = session.ReadyConfiguration;
        ApplyArena(ready, session.ObserverReadyConfiguration);
        ApplyActiveArea(session.LatestWorldOverview);
        ApplyControlled(ready);
        bool isObserver = session.Mode == RemoteGameplaySessionMode.Observer;
        worldOverviewPresentation.Apply(
            session.LatestWorldOverview,
            PixelsPerUnit,
            isObserver);
        if (isObserver)
        {
            ClearRemoteReplicas();
        }
        else
        {
            ApplyRemoteReplicas(nowSeconds);
        }
    }

    private List<RemovalPresentationCue> CaptureRemovalCues(
        RemoteGameplayDrainResult drainResult)
    {
        List<RemovalPresentationCue> cues = new List<RemovalPresentationCue>();
        for (int index = 0; index < drainResult.RemovalNotices.Count; ++index)
        {
            RemoteGameplayRemovalNotice notice = drainResult.RemovalNotices[index];
            GameplayReplicaNode? removedNode = null;
            if (controlledKey.HasValue &&
                controlledKey.Value == notice.Key &&
                controlledNode is not null)
            {
                removedNode = controlledNode;
            }
            else
            {
                remoteNodes.TryGetValue(notice.Key, out removedNode);
            }

            if (removedNode is null)
            {
                continue;
            }

            if (!GameplayPresentationCueProjector.ShouldPresentRemoval(
                    removedNode.EntityKind,
                    notice.Reason))
            {
                continue;
            }

            cues.Add(new RemovalPresentationCue(
                notice.Reason,
                removedNode.Position,
                removedNode.DisplayRadiusPixels));
        }
        return cues;
    }

    private void ApplyRemovalCues(
        IReadOnlyList<RemovalPresentationCue> removalCues)
    {
        for (int index = 0; index < removalCues.Count; ++index)
        {
            RemovalPresentationCue cue = removalCues[index];
            if (cue.Reason == EntityRemoveReason.Collected)
            {
                effects.Play(
                    GameplayOneShotEffectKind.Collected,
                    cue.Position,
                    cue.RadiusPixels);
                audio.PlayCollected();
            }
            else if (cue.Reason == EntityRemoveReason.Destroyed)
            {
                effects.Play(
                    GameplayOneShotEffectKind.Destroyed,
                    cue.Position,
                    cue.RadiusPixels);
                audio.PlayDestroyed();
            }
        }
    }

    private void ApplyStateCues()
    {
        if (presentationTransportGeneration.HasValue &&
            presentationTransportGeneration.Value != session.TransportGeneration)
        {
            effects.ClearTransientEffects();
            audio.SetBoost(false);
        }
        presentationTransportGeneration = session.TransportGeneration;

        GameplayPresentationObservation observation =
            GameplayPresentationObservation.From(session);
        IReadOnlyList<GameplayPresentationCue> cues = cueProjector.Project(observation);
        for (int index = 0; index < cues.Count; ++index)
        {
            GameplayPresentationCue cue = cues[index];
            switch (cue.Kind)
            {
                case GameplayPresentationCueKind.ControlledPresentationReset:
                    effects.ClearTransientEffects();
                    audio.SetBoost(false);
                    break;
                case GameplayPresentationCueKind.ControlledSpawn:
                    if (controlledNode is not null)
                    {
                        effects.Play(
                            GameplayOneShotEffectKind.Spawn,
                            controlledNode.Position,
                            controlledNode.DisplayRadiusPixels);
                    }
                    audio.PlaySpawn();
                    break;
                case GameplayPresentationCueKind.GrowthChanged:
                    hud.PulseGrowth();
                    if (controlledNode is not null)
                    {
                        effects.Play(
                            GameplayOneShotEffectKind.Growth,
                            controlledNode.Position,
                            controlledNode.DisplayRadiusPixels);
                    }
                    audio.PlayGrowth();
                    break;
                case GameplayPresentationCueKind.BoostStarted:
                    audio.SetBoost(true);
                    break;
                case GameplayPresentationCueKind.BoostStopped:
                    audio.SetBoost(false);
                    break;
                case GameplayPresentationCueKind.RoundResult:
                    audio.PlayResult(cue.RecipientWon);
                    break;
            }
        }
    }

    private void ApplyActiveAreaWarning(double nowSeconds)
    {
        bool isNearBoundary =
            flow.State == GameplayFlowState.Playing &&
            controlledNode is not null &&
            arenaPresentation.IsNearActiveAreaBoundary(
                controlledNode.Position,
                PixelsPerUnit * 2.25f);
        hud.SetActiveAreaWarning(isNearBoundary);
        if (isNearBoundary)
        {
            audio.PlayWarning(nowSeconds);
        }
    }

    private void ApplyActiveArea(WorldOverviewState? overview)
    {
        arenaPresentation.ApplyActiveArea(overview, PixelsPerUnit);
    }

    private void ApplyArena(WorldReady? ready, ObserverReady? observerReady)
    {
        bool isObserver = session.Mode == RemoteGameplaySessionMode.Observer;
        if (isObserver)
        {
            arenaPresentation.ApplyArena(observerReady, PixelsPerUnit);
        }
        else
        {
            arenaPresentation.ApplyArena(ready, PixelsPerUnit);
        }
        if (controlledNode is null && arenaPresentation.ArenaRect.HasValue)
        {
            Rect2 arena = arenaPresentation.ArenaRect.Value;
            camera.Position = arena.GetCenter();
            if (isObserver)
            {
                Vector2 viewportSize = GetViewportRect().Size;
                float fitZoom = Mathf.Min(
                    viewportSize.X / arena.Size.X,
                    viewportSize.Y / arena.Size.Y) * 0.92f;
                camera.Zoom = Vector2.One * Mathf.Max(0.05f, fitZoom);
            }
            else
            {
                camera.Zoom = Vector2.One;
            }
        }
    }

    private void ApplyControlled(WorldReady? ready)
    {
        ControlledEntityPredictionSnapshot? prediction =
            session.ControlledPrediction;
        float? controlledRadius = session.ControlledRadius;
        if (ready is null || !prediction.HasValue || !controlledRadius.HasValue)
        {
            RemoveControlledNode();
            return;
        }

        ClientWorldEntityKey nextKey = new ClientWorldEntityKey(
            ready.ControlledEntityId,
            ready.ControlledEntityGeneration);
        if (!controlledKey.HasValue ||
            controlledKey.Value != nextKey ||
            controlledNode is null)
        {
            RemoveControlledNode();
            controlledBodyLine = BodyLineScene.Instantiate<GameplayBodyLine>();
            controlledBodyLine.Configure(nextKey, isControlled: true);
            worldEntities.AddChild(controlledBodyLine);
            controlledNode = ReplicaScene.Instantiate<GameplayReplicaNode>();
            controlledNode.Configure(
                nextKey,
                EntityKind.Player,
                controlledRadius.Value,
                PixelsPerUnit,
                isControlled: true);
            worldEntities.AddChild(controlledNode);
            controlledKey = nextKey;
        }

        ControlledEntityPredictionSnapshot current = prediction.Value;
        controlledNode.ApplyRadius(
            controlledRadius.Value,
            PixelsPerUnit);
        controlledNode.ApplyTransform(
            current.RenderPosition,
            current.AngleRadians,
            PixelsPerUnit);
        string controlledLabel = string.IsNullOrEmpty(session.DisplayName)
            ? $"PLAYER {ready.PlayerId}"
            : session.DisplayName;
        controlledNode.ApplyPlayerLabel(controlledLabel);
        Vector2 lookAhead = Vector2.Right.Rotated(controlledNode.Rotation) * 40.0f;
        camera.Position = controlledNode.Position + lookAhead;
        ApplyControlledBody(current.RenderPosition, ready);

        ControlledEntityStateV2? state = session.LatestControlledStateV2;
        bool boostActive = state is not null &&
            state.ControlledEntityGeneration == ready.ControlledEntityGeneration &&
            state.BoostState == BoostStateV2.On;
        controlledNode.ApplyBoost(boostActive);
        controlledBodyLine?.ApplyBoost(boostActive);
    }

    private void ApplyControlledBody(
        System.Numerics.Vector2 renderedHead,
        WorldReady ready)
    {
        ControlledEntityStateV2? state = session.LatestControlledStateV2;
        if (state is null ||
            state.ControlledEntityGeneration != ready.ControlledEntityGeneration)
        {
            ClearControlledBody();
            return;
        }

        controlledBodyLine?.Apply(
            GodotWorldTransform.ControlledBodyTrail(
                renderedHead,
                state.BodyTrailSamples,
                PixelsPerUnit),
            state.Diameter * PixelsPerUnit);
    }

    private void ApplyRemoteReplicas(double nowSeconds)
    {
        IReadOnlyList<RemoteReplicaPresentationSnapshot> snapshots =
            Array.Empty<RemoteReplicaPresentationSnapshot>();
        session.TryBuildRemotePresentation(nowSeconds, out snapshots);

        HashSet<ClientWorldEntityKey> visibleKeys =
            new HashSet<ClientWorldEntityKey>();
        for (int index = 0; index < snapshots.Count; ++index)
        {
            RemoteReplicaPresentationSnapshot snapshot = snapshots[index];
            visibleKeys.Add(snapshot.Key);
            if (!remoteNodes.TryGetValue(
                    snapshot.Key,
                    out GameplayReplicaNode? node))
            {
                node = ReplicaScene.Instantiate<GameplayReplicaNode>();
                node.Configure(
                    snapshot.Key,
                    snapshot.EntityKind,
                    snapshot.Radius,
                    PixelsPerUnit,
                    isControlled: false);
                worldEntities.AddChild(node);
                remoteNodes.Add(snapshot.Key, node);
            }

            node.ApplyRadius(snapshot.Radius, PixelsPerUnit);
            node.ApplyTransform(
                snapshot.HeadPosition,
                snapshot.HeadingRadians,
                PixelsPerUnit);
            node.ApplyPlayerLabel(
                session.TryGetPlayerLabel(snapshot.Key, out string remoteLabel)
                    ? remoteLabel
                    : string.Empty);
            bool boostActive = snapshot.WholeBody is not null &&
                snapshot.WholeBody.BoostState == BoostStateV2.On;
            node.ApplyBoost(boostActive);
            if (snapshot.WholeBody is not null)
            {
                if (!remoteBodyLines.TryGetValue(
                        snapshot.Key,
                        out GameplayBodyLine? bodyLine))
                {
                    bodyLine = BodyLineScene.Instantiate<GameplayBodyLine>();
                    bodyLine.Configure(snapshot.Key, isControlled: false);
                    worldEntities.AddChild(bodyLine);
                    remoteBodyLines.Add(snapshot.Key, bodyLine);
                }
                bodyLine.Apply(
                    GodotWorldTransform.RemoteBodyTrail(
                        snapshot.WholeBody.HeadPosition,
                        snapshot.WholeBody.BodyTrail,
                        PixelsPerUnit),
                    snapshot.WholeBody.Diameter * PixelsPerUnit);
                bodyLine.ApplyBoost(boostActive);
            }
            else if (remoteBodyLines.TryGetValue(
                         snapshot.Key,
                         out GameplayBodyLine? bodyLine))
            {
                bodyLine.Clear();
                bodyLine.ApplyBoost(false);
            }
        }

        List<ClientWorldEntityKey> staleKeys =
            new List<ClientWorldEntityKey>();
        foreach (ClientWorldEntityKey key in remoteNodes.Keys)
        {
            if (!visibleKeys.Contains(key))
            {
                staleKeys.Add(key);
            }
        }
        for (int index = 0; index < staleKeys.Count; ++index)
        {
            ClientWorldEntityKey staleKey = staleKeys[index];
            remoteNodes[staleKey].QueueFree();
            remoteNodes.Remove(staleKey);
            if (remoteBodyLines.Remove(staleKey, out GameplayBodyLine? staleBodyLine))
            {
                staleBodyLine.QueueFree();
            }
        }
    }

    private void RemoveControlledNode()
    {
        controlledNode?.QueueFree();
        controlledBodyLine?.QueueFree();
        controlledNode = null;
        controlledBodyLine = null;
        controlledKey = null;
        hud?.SetActiveAreaWarning(false);
        if (arenaPresentation?.ArenaRect.HasValue == true)
        {
            camera.Position = arenaPresentation.ArenaRect.Value.GetCenter();
        }
    }

    private void ClearRemoteReplicas()
    {
        foreach (GameplayReplicaNode node in remoteNodes.Values)
        {
            node.QueueFree();
        }
        foreach (GameplayBodyLine bodyLine in remoteBodyLines.Values)
        {
            bodyLine.QueueFree();
        }
        remoteNodes.Clear();
        remoteBodyLines.Clear();
    }

    private void ClearControlledBody()
    {
        controlledBodyLine?.Clear();
    }

}
