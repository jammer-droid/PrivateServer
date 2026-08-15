using System;

namespace PrivateServer.NetworkRuntime.Managed;

public enum NetworkRuntimeErrorCode : uint
{
    Success = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    OutOfMemory = 3,
    CapacityExceeded = 4,
    QueueFull = 5,
    QueueEmpty = 6,
    IoFailed = 7,
    OperationCanceled = 8,
    ProtocolError = 9,
    Unknown = uint.MaxValue,
}

public enum NetworkRuntimeEventKind : uint
{
    None = 0,
    TransportConnected = 1,
    TransportConnectionFailed = 2,
    PacketReceived = 3,
    TransportDisconnected = 4,
    Unknown = uint.MaxValue,
}

public enum NetworkRuntimeDisconnectReason : uint
{
    None = 0,
    LocalRequested = 1,
    RemoteClosed = 2,
    ReceivePressure = 3,
    TransportError = 4,
    ProtocolError = 5,
    Unknown = uint.MaxValue,
}

public enum NetworkRuntimeClientLifecycleState : uint
{
    Invalid = 0,
    Idle = 1,
    TransportConnecting = 2,
    TransportConnected = 3,
    TransportDisconnecting = 4,
    Shutdown = 5,
    Unknown = uint.MaxValue,
}

public readonly struct NetworkRuntimeStatus
{
    internal NetworkRuntimeStatus(NativeMethods.NativeStatus nativeStatus)
    {
        ErrorCode = Enum.IsDefined(typeof(NetworkRuntimeErrorCode), nativeStatus.ErrorCode)
            ? (NetworkRuntimeErrorCode)nativeStatus.ErrorCode
            : NetworkRuntimeErrorCode.Unknown;
        NativeErrorCode = nativeStatus.NativeErrorCode;
    }

    public NetworkRuntimeErrorCode ErrorCode { get; }

    public uint NativeErrorCode { get; }

    public bool Succeeded => ErrorCode == NetworkRuntimeErrorCode.Success;
}

public readonly struct NetworkRuntimeClientConfig
{
    public NetworkRuntimeClientConfig(uint eventQueueCapacity, uint payloadQueueCapacity)
    {
        EventQueueCapacity = eventQueueCapacity;
        PayloadQueueCapacity = payloadQueueCapacity;
    }

    public uint EventQueueCapacity { get; }

    public uint PayloadQueueCapacity { get; }

    public static NetworkRuntimeClientConfig Default
    {
        get
        {
            NativeMethods.NativeClientConfig config = NativeMethods.psnr_client_config_default();
            return new NetworkRuntimeClientConfig(config.EventQueueCapacity, config.PayloadQueueCapacity);
        }
    }

    internal NativeMethods.NativeClientConfig ToNative()
    {
        return new NativeMethods.NativeClientConfig
        {
            EventQueueCapacity = EventQueueCapacity,
            PayloadQueueCapacity = PayloadQueueCapacity,
        };
    }
}

public readonly struct NetworkRuntimeIpv4Endpoint
{
    public NetworkRuntimeIpv4Endpoint(byte address0, byte address1, byte address2, byte address3, ushort port)
    {
        Address0 = address0;
        Address1 = address1;
        Address2 = address2;
        Address3 = address3;
        Port = port;
    }

    public byte Address0 { get; }

    public byte Address1 { get; }

    public byte Address2 { get; }

    public byte Address3 { get; }

    public ushort Port { get; }

    public static NetworkRuntimeIpv4Endpoint Loopback(ushort port)
    {
        return new NetworkRuntimeIpv4Endpoint(127, 0, 0, 1, port);
    }

    internal NativeMethods.NativeIpv4Endpoint ToNative()
    {
        return new NativeMethods.NativeIpv4Endpoint
        {
            Address0 = Address0,
            Address1 = Address1,
            Address2 = Address2,
            Address3 = Address3,
            Port = Port,
            Reserved = 0,
        };
    }
}

public readonly struct NetworkRuntimeClientSnapshot
{
    internal NetworkRuntimeClientSnapshot(NativeMethods.NativeClientSnapshot snapshot)
    {
        LifecycleState = Enum.IsDefined(typeof(NetworkRuntimeClientLifecycleState), snapshot.LifecycleState)
            ? (NetworkRuntimeClientLifecycleState)snapshot.LifecycleState
            : NetworkRuntimeClientLifecycleState.Unknown;
        PendingConnectIoCount = snapshot.PendingConnectIoCount;
        PendingRecvIoCount = snapshot.PendingRecvIoCount;
        PendingSendIoCount = snapshot.PendingSendIoCount;
        PendingIoCount = snapshot.PendingIoCount;
        EventQueueDepth = snapshot.EventQueueDepth;
        EventQueueHighWatermark = snapshot.EventQueueHighWatermark;
        PendingSendQueueDepth = snapshot.PendingSendQueueDepth;
        PendingSendQueueHighWatermark = snapshot.PendingSendQueueHighWatermark;
    }

    public NetworkRuntimeClientLifecycleState LifecycleState { get; }
    public ulong PendingConnectIoCount { get; }
    public ulong PendingRecvIoCount { get; }
    public ulong PendingSendIoCount { get; }
    public ulong PendingIoCount { get; }
    public ulong EventQueueDepth { get; }
    public ulong EventQueueHighWatermark { get; }
    public ulong PendingSendQueueDepth { get; }
    public ulong PendingSendQueueHighWatermark { get; }
}

public sealed class NetworkRuntimeException : Exception
{
    internal NetworkRuntimeException(NetworkRuntimeStatus status)
        : base($"NetworkRuntime operation failed with {status.ErrorCode} (native={status.NativeErrorCode}).")
    {
        Status = status;
    }

    public NetworkRuntimeStatus Status { get; }
}
