using PrivateServer.NetworkRuntime.Managed;
using System;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

const int PacketHeaderLength = 6;
const int MaxPacketLength = 8192;
const byte ProtocolVersion = 1;
const uint SmokePacketType = 77;
TimeSpan operationTimeout = TimeSpan.FromSeconds(5);

try
{
    if (args.Length > 1 || (args.Length == 1 && !string.Equals(args[0], "self-test", StringComparison.Ordinal)))
    {
        throw new ArgumentException("Usage: [self-test]");
    }

    await RunSelfTestAsync(operationTimeout);
    Console.WriteLine("PASS: managed packet round trip and teardown");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine($"FAIL: {exception}");
    return 1;
}

static async Task RunSelfTestAsync(TimeSpan timeout)
{
    TcpListener listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    ushort port = checked((ushort)((IPEndPoint)listener.LocalEndpoint).Port);
    using CancellationTokenSource peerTimeout = new CancellationTokenSource(timeout);
    Task peerTask = EchoOneFrameAsync(listener, peerTimeout.Token);

    try
    {
        NetworkRuntimeClient client = new NetworkRuntimeClient();
        try
        {
            AssertStatus(client.CaptureSnapshot(out NetworkRuntimeClientSnapshot idleSnapshot),
                NetworkRuntimeErrorCode.Success, "capture idle snapshot");
            AssertLifecycle(idleSnapshot, NetworkRuntimeClientLifecycleState.Idle, "idle snapshot");

            AssertStatus(client.Connect(NetworkRuntimeIpv4Endpoint.Loopback(port)), NetworkRuntimeErrorCode.Success,
                "connect admission");
            NetworkRuntimeEvent connectedEvent = WaitForEvent(client, timeout, "connect completion");
            AssertEventKind(connectedEvent, NetworkRuntimeEventKind.TransportConnected, "connect completion");

            byte[] expectedPayload = Encoding.UTF8.GetBytes("managed-smoke-payload");
            AssertStatus(client.Send(SmokePacketType, expectedPayload), NetworkRuntimeErrorCode.Success,
                "send admission");

            NetworkRuntimeEvent packetEvent = WaitForEvent(client, timeout, "packet receive");
            AssertEventKind(packetEvent, NetworkRuntimeEventKind.PacketReceived, "packet receive");
            if (packetEvent.PacketType != SmokePacketType)
            {
                throw new InvalidOperationException(
                    $"packet receive: expected type {SmokePacketType}, actual {packetEvent.PacketType}");
            }

            ReadOnlyMemory<byte> survivingPayload = packetEvent.Payload;
            NetworkRuntimeEvent disconnectedEvent = WaitForEvent(client, timeout, "remote disconnect");
            AssertEventKind(disconnectedEvent, NetworkRuntimeEventKind.TransportDisconnected, "remote disconnect");
            if (disconnectedEvent.DisconnectReason != NetworkRuntimeDisconnectReason.RemoteClosed)
            {
                throw new InvalidOperationException(
                    $"remote disconnect: expected RemoteClosed, actual {disconnectedEvent.DisconnectReason}");
            }

            if (!survivingPayload.Span.SequenceEqual(expectedPayload))
            {
                throw new InvalidOperationException("managed payload changed after native event destruction");
            }

            AssertStatus(client.Shutdown(), NetworkRuntimeErrorCode.Success, "shutdown");
            AssertStatus(client.Shutdown(), NetworkRuntimeErrorCode.Success, "repeated shutdown");
        }
        finally
        {
            client.Dispose();
            client.Dispose();
        }

        await peerTask;
    }
    finally
    {
        listener.Stop();
    }
}

static async Task EchoOneFrameAsync(TcpListener listener, CancellationToken cancellationToken)
{
    using TcpClient peer = await listener.AcceptTcpClientAsync(cancellationToken);
    await using NetworkStream stream = peer.GetStream();

    byte[] header = new byte[PacketHeaderLength];
    await stream.ReadExactlyAsync(header, cancellationToken);
    int packetLength = header[0] | (header[1] << 8);
    if (packetLength < PacketHeaderLength || packetLength > MaxPacketLength)
    {
        throw new InvalidOperationException($"peer received invalid packet length {packetLength}");
    }

    if (header[4] != ProtocolVersion || header[5] != 0)
    {
        throw new InvalidOperationException(
            $"peer received invalid protocol header version={header[4]}, flags={header[5]}");
    }

    byte[] frame = new byte[packetLength];
    header.CopyTo(frame, 0);
    await stream.ReadExactlyAsync(frame.AsMemory(PacketHeaderLength), cancellationToken);
    await stream.WriteAsync(frame, cancellationToken);
}

static NetworkRuntimeEvent WaitForEvent(NetworkRuntimeClient client, TimeSpan timeout, string step)
{
    Stopwatch stopwatch = Stopwatch.StartNew();
    while (stopwatch.Elapsed < timeout)
    {
        if (client.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
        {
            return clientEvent ?? throw new InvalidOperationException($"{step}: event was null");
        }

        Thread.Sleep(1);
    }

    AssertStatus(client.CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot),
        NetworkRuntimeErrorCode.Success, $"{step} timeout snapshot");
    throw new TimeoutException(
        $"{step}: timed out; lifecycle={snapshot.LifecycleState}, pendingIo={snapshot.PendingIoCount}, " +
        $"eventDepth={snapshot.EventQueueDepth}");
}

static void AssertStatus(NetworkRuntimeStatus status, NetworkRuntimeErrorCode expected, string step)
{
    if (status.ErrorCode != expected)
    {
        throw new InvalidOperationException($"{step}: expected {expected}, actual {status.ErrorCode} " +
            $"(native={status.NativeErrorCode})");
    }
}

static void AssertLifecycle(
    NetworkRuntimeClientSnapshot snapshot,
    NetworkRuntimeClientLifecycleState expected,
    string step)
{
    if (snapshot.LifecycleState != expected)
    {
        throw new InvalidOperationException(
            $"{step}: expected lifecycle {expected}, actual {snapshot.LifecycleState}");
    }
}

static void AssertEventKind(NetworkRuntimeEvent clientEvent, NetworkRuntimeEventKind expected, string step)
{
    if (clientEvent.Kind != expected)
    {
        string transport = clientEvent.TransportStatus.HasValue
            ? $", transport={clientEvent.TransportStatus.Value.ErrorCode} " +
              $"(native={clientEvent.TransportStatus.Value.NativeErrorCode})"
            : string.Empty;
        throw new InvalidOperationException(
            $"{step}: expected event {expected}, actual {clientEvent.Kind}{transport}");
    }
}
