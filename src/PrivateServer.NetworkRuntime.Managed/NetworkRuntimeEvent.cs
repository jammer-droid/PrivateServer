using System;

namespace PrivateServer.NetworkRuntime.Managed;

public sealed class NetworkRuntimeEvent
{
    internal NetworkRuntimeEvent(
        NetworkRuntimeEventKind kind,
        uint? packetType,
        ReadOnlyMemory<byte> payload,
        NetworkRuntimeStatus? transportStatus,
        NetworkRuntimeDisconnectReason? disconnectReason)
    {
        Kind = kind;
        PacketType = packetType;
        Payload = payload;
        TransportStatus = transportStatus;
        DisconnectReason = disconnectReason;
    }

    public NetworkRuntimeEventKind Kind { get; }
    public uint? PacketType { get; }
    public ReadOnlyMemory<byte> Payload { get; }
    public NetworkRuntimeStatus? TransportStatus { get; }
    public NetworkRuntimeDisconnectReason? DisconnectReason { get; }
}
