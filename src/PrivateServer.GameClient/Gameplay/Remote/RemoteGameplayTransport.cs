using PrivateServer.NetworkRuntime.Managed;
using System;

namespace PrivateServer.GameClient.Gameplay.Remote;

internal readonly record struct RemoteGameplayTransportStatus(
    NetworkRuntimeErrorCode ErrorCode,
    uint NativeErrorCode)
{
    internal bool Succeeded => ErrorCode == NetworkRuntimeErrorCode.Success;

    internal static RemoteGameplayTransportStatus From(NetworkRuntimeStatus status)
    {
        return new RemoteGameplayTransportStatus(status.ErrorCode, status.NativeErrorCode);
    }
}

internal enum RemoteGameplayTransportEventKind
{
    TransportConnected,
    TransportConnectionFailed,
    PacketReceived,
    TransportDisconnected,
}

internal sealed record RemoteGameplayTransportEvent(
    RemoteGameplayTransportEventKind Kind,
    uint? PacketType,
    ReadOnlyMemory<byte> Payload,
    RemoteGameplayTransportStatus? TransportStatus,
    NetworkRuntimeDisconnectReason? DisconnectReason)
{
    internal static RemoteGameplayTransportEvent Connected()
    {
        return new RemoteGameplayTransportEvent(
            RemoteGameplayTransportEventKind.TransportConnected,
            null,
            ReadOnlyMemory<byte>.Empty,
            null,
            null);
    }

    internal static RemoteGameplayTransportEvent ConnectionFailed(
        RemoteGameplayTransportStatus status)
    {
        return new RemoteGameplayTransportEvent(
            RemoteGameplayTransportEventKind.TransportConnectionFailed,
            null,
            ReadOnlyMemory<byte>.Empty,
            status,
            null);
    }

    internal static RemoteGameplayTransportEvent Packet(uint packetType, ReadOnlyMemory<byte> payload)
    {
        return new RemoteGameplayTransportEvent(
            RemoteGameplayTransportEventKind.PacketReceived,
            packetType,
            payload,
            null,
            null);
    }

    internal static RemoteGameplayTransportEvent Disconnected(
        RemoteGameplayTransportStatus status,
        NetworkRuntimeDisconnectReason reason)
    {
        return new RemoteGameplayTransportEvent(
            RemoteGameplayTransportEventKind.TransportDisconnected,
            null,
            ReadOnlyMemory<byte>.Empty,
            status,
            reason);
    }
}

internal interface IRemoteGameplayTransport : IDisposable
{
    RemoteGameplayTransportStatus Connect(NetworkRuntimeIpv4Endpoint endpoint);
    RemoteGameplayTransportStatus Disconnect();
    RemoteGameplayTransportStatus Shutdown();
    RemoteGameplayTransportStatus Send(uint packetType, ReadOnlySpan<byte> payload);
    bool TryPopEvent(out RemoteGameplayTransportEvent? transportEvent);
}

internal sealed class NetworkRuntimeGameplayTransport : IRemoteGameplayTransport
{
    private readonly NetworkRuntimeClient client;

    internal static NetworkRuntimeClientConfig GameplayClientConfig { get; } =
        new NetworkRuntimeClientConfig(
            eventQueueCapacity: 512,
            payloadQueueCapacity: 1024);

    internal NetworkRuntimeGameplayTransport()
        : this(new NetworkRuntimeClient(GameplayClientConfig))
    {
    }

    internal NetworkRuntimeGameplayTransport(NetworkRuntimeClient client)
    {
        ArgumentNullException.ThrowIfNull(client);
        this.client = client;
    }

    public RemoteGameplayTransportStatus Connect(NetworkRuntimeIpv4Endpoint endpoint)
    {
        return RemoteGameplayTransportStatus.From(client.Connect(endpoint));
    }

    public RemoteGameplayTransportStatus Disconnect()
    {
        return RemoteGameplayTransportStatus.From(client.Disconnect());
    }

    public RemoteGameplayTransportStatus Shutdown()
    {
        return RemoteGameplayTransportStatus.From(client.Shutdown());
    }

    public RemoteGameplayTransportStatus Send(uint packetType, ReadOnlySpan<byte> payload)
    {
        return RemoteGameplayTransportStatus.From(client.Send(packetType, payload));
    }

    public bool TryPopEvent(out RemoteGameplayTransportEvent? transportEvent)
    {
        transportEvent = null;
        if (!client.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
        {
            return false;
        }
        if (clientEvent is null)
        {
            throw new InvalidOperationException("NetworkRuntime returned a null event.");
        }

        transportEvent = Convert(clientEvent);
        return true;
    }

    public void Dispose()
    {
        client.Dispose();
    }

    private static RemoteGameplayTransportEvent Convert(NetworkRuntimeEvent clientEvent)
    {
        switch (clientEvent.Kind)
        {
            case NetworkRuntimeEventKind.TransportConnected:
                return RemoteGameplayTransportEvent.Connected();
            case NetworkRuntimeEventKind.TransportConnectionFailed:
                if (!clientEvent.TransportStatus.HasValue)
                {
                    throw new InvalidOperationException(
                        "TransportConnectionFailed event is missing its transport status.");
                }
                return RemoteGameplayTransportEvent.ConnectionFailed(
                    RemoteGameplayTransportStatus.From(clientEvent.TransportStatus.Value));
            case NetworkRuntimeEventKind.PacketReceived:
                if (!clientEvent.PacketType.HasValue)
                {
                    throw new InvalidOperationException(
                        "PacketReceived event is missing its packet type.");
                }
                return RemoteGameplayTransportEvent.Packet(
                    clientEvent.PacketType.Value,
                    clientEvent.Payload);
            case NetworkRuntimeEventKind.TransportDisconnected:
                if (!clientEvent.TransportStatus.HasValue || !clientEvent.DisconnectReason.HasValue)
                {
                    throw new InvalidOperationException(
                        "TransportDisconnected event is missing status or reason.");
                }
                return RemoteGameplayTransportEvent.Disconnected(
                    RemoteGameplayTransportStatus.From(clientEvent.TransportStatus.Value),
                    clientEvent.DisconnectReason.Value);
            default:
                throw new InvalidOperationException(
                    $"NetworkRuntime returned unsupported event kind {clientEvent.Kind}.");
        }
    }
}
