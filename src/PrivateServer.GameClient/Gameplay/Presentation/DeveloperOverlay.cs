using Godot;
using PrivateServer.GameClient.Gameplay.Flow;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Remote;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class DeveloperOverlay : CanvasLayer
{
    private string endpointDescription = "-";
    private Label endpointLabel = null!;
    private Label connectionLabel = null!;
    private Label identityLabel = null!;
    private Label tickLabel = null!;
    private Label replicaLabel = null!;
    private Label drainLabel = null!;
    private Label faultLabel = null!;

    public override void _Ready()
    {
        endpointLabel = GetNode<Label>("%EndpointLabel");
        connectionLabel = GetNode<Label>("%ConnectionLabel");
        identityLabel = GetNode<Label>("%IdentityLabel");
        tickLabel = GetNode<Label>("%TickLabel");
        replicaLabel = GetNode<Label>("%ReplicaLabel");
        drainLabel = GetNode<Label>("%DrainLabel");
        faultLabel = GetNode<Label>("%FaultLabel");
    }

    internal void ConfigureChannel(GameplayChannelOption channel)
    {
        endpointDescription =
            $"{channel.Name}  {channel.Address}:{channel.Port}";
    }

    internal void UpdatePresentation(
        RemoteGameplaySession session,
        double nowSeconds,
        RemoteGameplayDrainResult drainResult)
    {
        endpointLabel.Text = $"Endpoint: {endpointDescription}";
        connectionLabel.Text =
            $"Connection: {session.State}  transport generation: " +
            session.TransportGeneration;

        WorldReady? ready = session.ReadyConfiguration;
        identityLabel.Text = ready is null
            ? "Player ID: -  entity ID: -"
            : $"Player ID: {ready.PlayerId}  entity ID: " +
              $"{ready.ControlledEntityId}:{ready.ControlledEntityGeneration}";

        uint estimatedServerTick = 0;
        bool hasEstimatedTick =
            session.EstimateServerTick(nowSeconds, out estimatedServerTick) ==
            WorldTimeSyncError.None;
        tickLabel.Text =
            $"Server tick: {(hasEstimatedTick ? estimatedServerTick.ToString() : "-")}";
        replicaLabel.Text =
            $"Replicas: {session.RemoteReplicaCount}  resources: " +
            session.ResourceReplicaCount;
        drainLabel.Text =
            $"Drained: {drainResult.DrainedEventCount}/" +
            RemoteGameplaySession.DefaultMaxEventsPerFrame;

        string fault = session.LastFault is null
            ? "-"
            : $"{session.LastFault.Kind}: {session.LastFault.Message}";
        faultLabel.Text = $"Fault: {fault}";
    }
}
