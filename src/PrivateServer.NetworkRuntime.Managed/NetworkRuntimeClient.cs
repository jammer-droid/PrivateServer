using System;
using System.Runtime.InteropServices;

namespace PrivateServer.NetworkRuntime.Managed;

public sealed class NetworkRuntimeClient : IDisposable
{
    private readonly SafeClientHandle handle;

    public NetworkRuntimeClient()
        : this(NetworkRuntimeClientConfig.Default)
    {
    }

    public NetworkRuntimeClient(NetworkRuntimeClientConfig config)
    {
        NativeMethods.NativeClientConfig nativeConfig = config.ToNative();
        NativeMethods.NativeStatus nativeStatus = NativeMethods.psnr_client_create(nativeConfig, out IntPtr client);
        NetworkRuntimeStatus status = new NetworkRuntimeStatus(nativeStatus);
        if (!status.Succeeded)
        {
            throw new NetworkRuntimeException(status);
        }

        handle = new SafeClientHandle(client);
    }

    public NetworkRuntimeStatus Connect(NetworkRuntimeIpv4Endpoint endpoint)
    {
        ThrowIfDisposed();
        if (endpoint.Port == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(endpoint), "The endpoint port must not be zero.");
        }

        NativeMethods.NativeIpv4Endpoint nativeEndpoint = endpoint.ToNative();
        NativeMethods.NativeStatus nativeStatus = NativeMethods.psnr_client_connect_ipv4(handle, nativeEndpoint);
        return new NetworkRuntimeStatus(nativeStatus);
    }

    public NetworkRuntimeStatus Disconnect()
    {
        ThrowIfDisposed();
        return new NetworkRuntimeStatus(NativeMethods.psnr_client_disconnect(handle));
    }

    public NetworkRuntimeStatus Shutdown()
    {
        ThrowIfDisposed();
        return new NetworkRuntimeStatus(NativeMethods.psnr_client_shutdown(handle));
    }

    public unsafe NetworkRuntimeStatus Send(uint packetType, ReadOnlySpan<byte> payload)
    {
        ThrowIfDisposed();
        fixed (byte* payloadPointer = payload)  // fix address
        {
            NativeMethods.NativeStatus nativeStatus = NativeMethods.psnr_client_send(
                handle,
                packetType,
                (IntPtr)payloadPointer,
                checked((uint)payload.Length));
            return new NetworkRuntimeStatus(nativeStatus);
        }
    }

    public bool TryPopEvent(out NetworkRuntimeEvent? clientEvent)
    {
        ThrowIfDisposed();
        clientEvent = null;

        NativeMethods.NativeStatus nativeStatus = NativeMethods.psnr_client_try_pop_event(handle, out IntPtr eventPointer);
        NetworkRuntimeStatus status = new NetworkRuntimeStatus(nativeStatus);
        if (status.ErrorCode == NetworkRuntimeErrorCode.QueueEmpty)
        {
            return false;
        }

        if (!status.Succeeded)
        {
            throw new NetworkRuntimeException(status);
        }

        if (eventPointer == IntPtr.Zero) // invalid address
        {
            throw new InvalidOperationException("Native event pop succeeded without returning an event handle.");
        }

        SafeClientEventHandle eventHandle = new SafeClientEventHandle(eventPointer);
        try
        {
            clientEvent = ReadEvent(eventHandle);
            return true;
        }
        finally
        {
            eventHandle.Dispose();
        }
    }

    public NetworkRuntimeStatus CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot)
    {
        ThrowIfDisposed();
        NativeMethods.NativeStatus nativeStatus =
            NativeMethods.psnr_client_capture_snapshot(handle, out NativeMethods.NativeClientSnapshot nativeSnapshot);
        NetworkRuntimeStatus status = new NetworkRuntimeStatus(nativeStatus);
        snapshot = status.Succeeded ? new NetworkRuntimeClientSnapshot(nativeSnapshot) : default;
        return status;
    }

    public void Dispose()
    {
        handle.Dispose();
    }

    private void ThrowIfDisposed()
    {
        if (handle.IsClosed)
        {
            throw new ObjectDisposedException(nameof(NetworkRuntimeClient));
        }
    }

    private static NetworkRuntimeEvent ReadEvent(SafeClientEventHandle eventHandle)
    {
        EnsureAccessorSucceeded(NativeMethods.psnr_client_event_get_kind(eventHandle, out uint nativeKind));
        NetworkRuntimeEventKind kind = Enum.IsDefined(typeof(NetworkRuntimeEventKind), nativeKind)
            ? (NetworkRuntimeEventKind)nativeKind
            : NetworkRuntimeEventKind.Unknown;

        switch (kind)
        {
            case NetworkRuntimeEventKind.TransportConnected:
                return new NetworkRuntimeEvent(kind, null, ReadOnlyMemory<byte>.Empty, null, null);
            case NetworkRuntimeEventKind.TransportConnectionFailed:
                EnsureAccessorSucceeded(
                    NativeMethods.psnr_client_event_get_transport_status(eventHandle, out NativeMethods.NativeStatus failureStatus));
                return new NetworkRuntimeEvent(kind, null, ReadOnlyMemory<byte>.Empty,
                    new NetworkRuntimeStatus(failureStatus), null);
            case NetworkRuntimeEventKind.PacketReceived:
                EnsureAccessorSucceeded(NativeMethods.psnr_client_event_get_packet_type(eventHandle, out uint packetType));
                EnsureAccessorSucceeded(
                    NativeMethods.psnr_client_event_get_payload(eventHandle, out NativeMethods.NativeByteView nativePayload));
                byte[] payload = CopyPayload(nativePayload);
                return new NetworkRuntimeEvent(kind, packetType, payload, null, null);
            case NetworkRuntimeEventKind.TransportDisconnected:
                EnsureAccessorSucceeded(
                    NativeMethods.psnr_client_event_get_transport_status(eventHandle, out NativeMethods.NativeStatus transportStatus));
                EnsureAccessorSucceeded(
                    NativeMethods.psnr_client_event_get_disconnect_reason(eventHandle, out uint nativeReason));
                NetworkRuntimeDisconnectReason reason = Enum.IsDefined(typeof(NetworkRuntimeDisconnectReason), nativeReason)
                    ? (NetworkRuntimeDisconnectReason)nativeReason
                    : NetworkRuntimeDisconnectReason.Unknown;
                return new NetworkRuntimeEvent(kind, null, ReadOnlyMemory<byte>.Empty,
                    new NetworkRuntimeStatus(transportStatus), reason);
            default:
                throw new InvalidOperationException($"Native event returned unsupported kind {nativeKind}.");
        }
    }

    private static byte[] CopyPayload(NativeMethods.NativeByteView nativePayload)
    {
        if (nativePayload.Size == 0)
        {
            return Array.Empty<byte>();
        }

        if (nativePayload.Data == IntPtr.Zero)
        {
            throw new InvalidOperationException("Native event returned a null pointer for a non-empty payload.");
        }

        int payloadSize = (int)nativePayload.Size;
        byte[] payload = new byte[payloadSize];
        Marshal.Copy(nativePayload.Data, payload, 0, payloadSize); // native to C#
        return payload;
    }

    private static void EnsureAccessorSucceeded(NativeMethods.NativeStatus nativeStatus)
    {
        NetworkRuntimeStatus status = new NetworkRuntimeStatus(nativeStatus);
        if (!status.Succeeded)
        {
            throw new NetworkRuntimeException(status);
        }
    }
}
