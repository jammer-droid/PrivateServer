using Godot;
using PrivateServer.NetworkRuntime.Managed;
using System.Diagnostics;

public partial class Pri57NetworkRuntimeFixture : Node
{
	private const ushort BenchmarkPort = 42057;
	private const uint BenchmarkClientId = 1;
	private const int MainThreadDrainBudget = 64;
	private const int BoundaryResponseCount = MainThreadDrainBudget + 1;
	private const ulong Generation2RoundtripSequence = BoundaryResponseCount + 1;
	private const double ConnectTimeoutSeconds = 30.0;
	private const double ResponseQueueTimeoutSeconds = 30.0;
	private const double RemoteCloseTimeoutSeconds = 30.0;
	private const double RoundtripTimeoutSeconds = 30.0;
	private const double LocalDisconnectTimeoutSeconds = 30.0;

	private enum FixtureState
	{
		NotStarted,
		WaitingForGeneration1Connect,
		WaitingForBoundaryResponses,
		BoundaryResponsesQueued,
		WaitingForFinalBoundaryDrain,
		WaitingForGeneration1RemoteClose,

		WaitingForGeneration2Connect,
		WaitingForGeneration2Roundtrip,
		WaitingForGeneration2LocalDisconnect,
		Completed,
		Failed,
	}

	private NetworkRuntimeClient? client;
	private readonly bool[] receivedBoundarySequences = new bool[BoundaryResponseCount + 1];
	private FixtureState state;
	private double connectElapsedSeconds;
	private double responseQueueElapsedSeconds;
	private double remoteCloseElapsedSeconds;
	private double roundtripElapsedSeconds;
	private double localDisconnectElapsedSeconds;
	private ulong generation2ClientSendTimestampNanoseconds;

	public override void _Ready()
	{
		client = new NetworkRuntimeClient();
		GD.Print("NetworkRuntime client created.");

		NetworkRuntimeStatus connectStatus =
			client.Connect(NetworkRuntimeIpv4Endpoint.Loopback(BenchmarkPort));
		if (!connectStatus.Succeeded)
		{
			Fail(
				$"Generation 1 connect request failed: {connectStatus.ErrorCode} " +
				$"(native={connectStatus.NativeErrorCode}).");
			return;
		}

		state = FixtureState.WaitingForGeneration1Connect;
		GD.Print("Generation 1 connect requested.");
	}

	public override void _Process(double delta)
	{
		if (state == FixtureState.WaitingForGeneration2LocalDisconnect)
		{
			ObserveGeneration2LocalDisconnect(delta);
			return;
		}

		if (state == FixtureState.WaitingForGeneration2Roundtrip)
		{
			ObserveGeneration2Roundtrip(delta);
			return;
		}

		if (state == FixtureState.WaitingForGeneration2Connect)
		{
			ObserveGeneration2Connect(delta);
			return;
		}

		if (state == FixtureState.WaitingForGeneration1RemoteClose)
		{
			ObserveGeneration1RemoteClose(delta);
			return;
		}

		if (state == FixtureState.BoundaryResponsesQueued)
		{
			DrainFirstBoundaryFrame();
			return;
		}

		if (state == FixtureState.WaitingForFinalBoundaryDrain)
		{
			DrainFinalBoundaryFrame();
			return;
		}

		if (state == FixtureState.WaitingForBoundaryResponses)
		{
			ObserveBoundaryResponseQueue(delta);
			return;
		}

		if (state != FixtureState.WaitingForGeneration1Connect)
		{
			return;
		}

		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Generation 1 connect failed because the client is unavailable.");
			return;
		}

		if (activeClient.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
		{
			HandleGeneration1ConnectEvent(activeClient, clientEvent);
			return;
		}

		connectElapsedSeconds += delta;
		if (connectElapsedSeconds >= ConnectTimeoutSeconds)
		{
			Fail($"Generation 1 connect timed out after {ConnectTimeoutSeconds:0} seconds.");
		}
	}

	public override void _ExitTree()
	{
		NetworkRuntimeClient? ownedClient = client;
		client = null;
		if (ownedClient is null)
		{
			return;
		}

		NetworkRuntimeStatus shutdownStatus;
		NetworkRuntimeStatus repeatedShutdownStatus;
		try
		{
			shutdownStatus = ownedClient.Shutdown();
			repeatedShutdownStatus = ownedClient.Shutdown();
		}
		finally
		{
			ownedClient.Dispose();
			ownedClient.Dispose();
		}

		if (!shutdownStatus.Succeeded || !repeatedShutdownStatus.Succeeded)
		{
			GD.PushError(
				$"NetworkRuntime repeated shutdown failed: " +
				$"first={shutdownStatus.ErrorCode} (native={shutdownStatus.NativeErrorCode}), " +
				$"second={repeatedShutdownStatus.ErrorCode} " +
				$"(native={repeatedShutdownStatus.NativeErrorCode}).");
			return;
		}

		GD.Print("NetworkRuntime client repeated shutdown and dispose completed.");
	}

	private void HandleGeneration1ConnectEvent(
		NetworkRuntimeClient activeClient,
		NetworkRuntimeEvent? clientEvent)
	{
		if (clientEvent is null)
		{
			Fail("Generation 1 connect returned a null event.");
			return;
		}

		if (clientEvent.Kind == NetworkRuntimeEventKind.TransportConnected)
		{
			// 연결 후 65회 send 요청 진행
			GD.Print("Generation 1 transport connected.");
			SubmitBoundaryRequests(activeClient);
			return;
		}

		if (clientEvent.Kind == NetworkRuntimeEventKind.TransportConnectionFailed)
		{
			NetworkRuntimeStatus? transportStatus = clientEvent.TransportStatus;
			string detail = transportStatus.HasValue
				? $"{transportStatus.Value.ErrorCode} (native={transportStatus.Value.NativeErrorCode})"
				: "missing transport status";
			Fail($"Generation 1 transport connection failed: {detail}.");
			return;
		}

		Fail($"Generation 1 connect received unexpected event: {clientEvent.Kind}.");
	}

	private void SubmitBoundaryRequests(NetworkRuntimeClient activeClient)
	{
		for (ulong sequence = 1; sequence <= BoundaryResponseCount; ++sequence)
		{
			byte[] payload = Pri57BenchmarkProtocol.EncodeRequest(
				BenchmarkClientId,
				sequence,
				SteadyNanosecondsNow());
			NetworkRuntimeStatus sendStatus =
				activeClient.Send(Pri57BenchmarkProtocol.RequestPacketType, payload);

			if (!sendStatus.Succeeded)
			{
				Fail(
					$"Boundary request {sequence} failed: {sendStatus.ErrorCode} " +
					$"(native={sendStatus.NativeErrorCode}).");
				return;
			}
		}

		state = FixtureState.WaitingForBoundaryResponses;
		responseQueueElapsedSeconds = 0;
		GD.Print($"{BoundaryResponseCount} boundary requests submitted.");
	}

	private void ObserveBoundaryResponseQueue(double delta)
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Boundary response observation failed because the client is unavailable.");
			return;
		}

		NetworkRuntimeStatus snapshotStatus =
			activeClient.CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot);
		if (!snapshotStatus.Succeeded)
		{
			Fail(
				$"Boundary response snapshot failed: {snapshotStatus.ErrorCode} " +
				$"(native={snapshotStatus.NativeErrorCode}).");
			return;
		}

		if (snapshot.EventQueueDepth == BoundaryResponseCount)
		{
			state = FixtureState.BoundaryResponsesQueued;
			GD.Print($"{BoundaryResponseCount} boundary responses queued.");
			return;
		}

		if (snapshot.EventQueueDepth > BoundaryResponseCount)
		{
			Fail(
				$"Boundary response queue exceeded {BoundaryResponseCount}: " +
				$"{snapshot.EventQueueDepth}.");
			return;
		}

		responseQueueElapsedSeconds += delta;
		if (responseQueueElapsedSeconds >= ResponseQueueTimeoutSeconds)
		{
			Fail(
				$"Boundary response queue timed out at depth {snapshot.EventQueueDepth} " +
				$"after {ResponseQueueTimeoutSeconds:0} seconds.");
		}
	}

	private void DrainFirstBoundaryFrame() // bounded queue 개수 확인 이후
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("First boundary drain failed because the client is unavailable.");
			return;
		}

		// 64개만 drain, 남은 1개는 다음 프레임에서 drain
		for (int drainIndex = 0; drainIndex < MainThreadDrainBudget; ++drainIndex)
		{
			if (!TryDrainBoundaryResponse(activeClient))
			{
				return;
			}
		}

		NetworkRuntimeStatus snapshotStatus =
			activeClient.CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot);
		if (!snapshotStatus.Succeeded)
		{
			Fail(
				$"First boundary drain snapshot failed: {snapshotStatus.ErrorCode} " +
				$"(native={snapshotStatus.NativeErrorCode}).");
			return;
		}

		if (snapshot.EventQueueDepth != 1)
		{
			Fail($"First boundary drain expected depth 1, actual {snapshot.EventQueueDepth}.");
			return;
		}

		state = FixtureState.WaitingForFinalBoundaryDrain;
		GD.Print("First boundary frame drained 64 responses; depth=1.");
	}

	private void DrainFinalBoundaryFrame() // 첫 64 패킷 처리 이후
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Final boundary drain failed because the client is unavailable.");
			return;
		}

		if (!TryDrainBoundaryResponse(activeClient))    // 마지막 패킷 drain
		{
			return;
		}

		NetworkRuntimeStatus snapshotStatus =
			activeClient.CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot);
		if (!snapshotStatus.Succeeded)
		{
			Fail(
				$"Final boundary drain snapshot failed: {snapshotStatus.ErrorCode} " +
				$"(native={snapshotStatus.NativeErrorCode}).");
			return;
		}

		if (snapshot.EventQueueDepth != 0)  // client eventQueue 비어있는지 확인
		{
			Fail($"Final boundary drain expected depth 0, actual {snapshot.EventQueueDepth}.");
			return;
		}

		state = FixtureState.WaitingForGeneration1RemoteClose;
		remoteCloseElapsedSeconds = 0;
		GD.Print("Final boundary frame drained 1 response; depth=0.");
	}

	private void ObserveGeneration1RemoteClose(double delta)    // 마지막 패킷 drain 이후
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Generation 1 remote close failed because the client is unavailable.");
			return;
		}

		if (!activeClient.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
		{
			remoteCloseElapsedSeconds += delta;
			if (remoteCloseElapsedSeconds >= RemoteCloseTimeoutSeconds)
			{
				Fail(
					$"Generation 1 remote close timed out after " +
					$"{RemoteCloseTimeoutSeconds:0} seconds.");
			}
			return;
		}

		if (clientEvent is null)
		{
			Fail("Generation 1 remote close returned a null event.");
			return;
		}

		if (clientEvent.Kind != NetworkRuntimeEventKind.TransportDisconnected)
		{
			Fail($"Generation 1 remote close received unexpected event: {clientEvent.Kind}.");
			return;
		}

		if (clientEvent.DisconnectReason != NetworkRuntimeDisconnectReason.RemoteClosed)
		{
			Fail(
				$"Generation 1 remote close expected reason RemoteClosed, " +
				$"actual {clientEvent.DisconnectReason}.");
			return;
		}

		NetworkRuntimeStatus? transportStatus = clientEvent.TransportStatus;
		if (!transportStatus.HasValue || !transportStatus.Value.Succeeded)
		{
			string detail = transportStatus.HasValue
				? $"{transportStatus.Value.ErrorCode} (native={transportStatus.Value.NativeErrorCode})"
				: "missing transport status";
			Fail($"Generation 1 remote close returned unsuccessful transport status: {detail}.");
			return;
		}

		GD.Print("Generation 1 remote close observed after all packet responses.");
		BeginGeneration2Connect(activeClient);
	}

	private void BeginGeneration2Connect(NetworkRuntimeClient activeClient)
	{
		NetworkRuntimeStatus snapshotStatus =
			activeClient.CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot);
		if (!snapshotStatus.Succeeded)
		{
			Fail(
				$"Generation 2 pre-connect snapshot failed: {snapshotStatus.ErrorCode} " +
				$"(native={snapshotStatus.NativeErrorCode}).");
			return;
		}

		if (snapshot.LifecycleState != NetworkRuntimeClientLifecycleState.Idle)
		{
			Fail(
				$"Generation 2 connect expected Idle lifecycle, " +
				$"actual {snapshot.LifecycleState}.");
			return;
		}

		NetworkRuntimeStatus connectStatus =
			activeClient.Connect(NetworkRuntimeIpv4Endpoint.Loopback(BenchmarkPort));
		if (!connectStatus.Succeeded)
		{
			Fail(
				$"Generation 2 connect request failed: {connectStatus.ErrorCode} " +
				$"(native={connectStatus.NativeErrorCode}).");
			return;
		}

		state = FixtureState.WaitingForGeneration2Connect;
		connectElapsedSeconds = 0;
		GD.Print("Generation 2 connect requested.");
	}

	private void ObserveGeneration2Connect(double delta)
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Generation 2 connect failed because the client is unavailable.");
			return;
		}

		if (!activeClient.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
		{
			connectElapsedSeconds += delta;
			if (connectElapsedSeconds >= ConnectTimeoutSeconds)
			{
				Fail($"Generation 2 connect timed out after {ConnectTimeoutSeconds:0} seconds.");
			}
			return;
		}

		if (clientEvent is null)
		{
			Fail("Generation 2 connect returned a null event.");
			return;
		}

		if (clientEvent.Kind == NetworkRuntimeEventKind.TransportConnected)
		{
			GD.Print("Generation 2 transport connected.");
			SubmitGeneration2Roundtrip(activeClient);
			return;
		}

		if (clientEvent.Kind == NetworkRuntimeEventKind.TransportConnectionFailed)
		{
			NetworkRuntimeStatus? transportStatus = clientEvent.TransportStatus;
			string detail = transportStatus.HasValue
				? $"{transportStatus.Value.ErrorCode} (native={transportStatus.Value.NativeErrorCode})"
				: "missing transport status";
			Fail($"Generation 2 transport connection failed: {detail}.");
			return;
		}

		Fail($"Generation 2 connect received unexpected event: {clientEvent.Kind}.");
	}

	private void SubmitGeneration2Roundtrip(NetworkRuntimeClient activeClient)
	{
		generation2ClientSendTimestampNanoseconds = SteadyNanosecondsNow();
		byte[] payload = Pri57BenchmarkProtocol.EncodeRequest(
			BenchmarkClientId,
			Generation2RoundtripSequence,
			generation2ClientSendTimestampNanoseconds);
		NetworkRuntimeStatus sendStatus =
			activeClient.Send(Pri57BenchmarkProtocol.RequestPacketType, payload);
		if (!sendStatus.Succeeded)
		{
			Fail(
				$"Generation 2 roundtrip request failed: {sendStatus.ErrorCode} " +
				$"(native={sendStatus.NativeErrorCode}).");
			return;
		}

		state = FixtureState.WaitingForGeneration2Roundtrip;
		roundtripElapsedSeconds = 0;
		GD.Print("Generation 2 roundtrip request submitted.");
	}

	private void ObserveGeneration2Roundtrip(double delta)
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Generation 2 roundtrip failed because the client is unavailable.");
			return;
		}

		if (!activeClient.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
		{
			roundtripElapsedSeconds += delta;
			if (roundtripElapsedSeconds >= RoundtripTimeoutSeconds)
			{
				Fail($"Generation 2 roundtrip timed out after {RoundtripTimeoutSeconds:0} seconds.");
			}
			return;
		}

		if (clientEvent is null ||
			clientEvent.Kind != NetworkRuntimeEventKind.PacketReceived ||
			clientEvent.PacketType != Pri57BenchmarkProtocol.ResponsePacketType)
		{
			Fail(
				$"Generation 2 roundtrip received unexpected event: " +
				$"kind={clientEvent?.Kind}, packetType={clientEvent?.PacketType}.");
			return;
		}

		if (!Pri57BenchmarkProtocol.TryDecodeResponse(
				clientEvent.Payload.Span,
				out Pri57BenchmarkResponse response,
				out string decodeError))
		{
			Fail($"Generation 2 roundtrip response decode failed: {decodeError}.");
			return;
		}

		if (response.ClientId != BenchmarkClientId ||
			response.Sequence != Generation2RoundtripSequence ||
			response.ClientSendTimestampNanoseconds != generation2ClientSendTimestampNanoseconds)
		{
			Fail(
				$"Generation 2 roundtrip identity mismatch: clientId={response.ClientId}, " +
				$"sequence={response.Sequence}, " +
				$"clientSend={response.ClientSendTimestampNanoseconds}.");
			return;
		}

		if (response.ServerReceivedTimestampNanoseconds == 0 ||
			response.ServerResponsePreparedTimestampNanoseconds <
			response.ServerReceivedTimestampNanoseconds)
		{
			Fail(
				$"Generation 2 server timestamps are invalid: " +
				$"received={response.ServerReceivedTimestampNanoseconds}, " +
				$"prepared={response.ServerResponsePreparedTimestampNanoseconds}.");
			return;
		}

		ulong clientObservedTimestampNanoseconds = SteadyNanosecondsNow();
		if (clientObservedTimestampNanoseconds < generation2ClientSendTimestampNanoseconds)
		{
			Fail("Generation 2 client monotonic timestamp moved backwards.");
			return;
		}

		ulong clientObservedRttNanoseconds =
			clientObservedTimestampNanoseconds - generation2ClientSendTimestampNanoseconds;
		ulong serverProcessingNanoseconds =
			response.ServerResponsePreparedTimestampNanoseconds -
			response.ServerReceivedTimestampNanoseconds;
		GD.Print(
			$"Generation 2 roundtrip completed: clientRttNs={clientObservedRttNanoseconds}, " +
			$"serverProcessingNs={serverProcessingNanoseconds}.");

		BeginGeneration2LocalDisconnect(activeClient);
	}

	private void BeginGeneration2LocalDisconnect(NetworkRuntimeClient activeClient)
	{
		NetworkRuntimeStatus disconnectStatus = activeClient.Disconnect();
		if (!disconnectStatus.Succeeded)
		{
			Fail(
				$"Generation 2 local disconnect request failed: {disconnectStatus.ErrorCode} " +
				$"(native={disconnectStatus.NativeErrorCode}).");
			return;
		}

		state = FixtureState.WaitingForGeneration2LocalDisconnect;
		localDisconnectElapsedSeconds = 0;
		GD.Print("Generation 2 local disconnect requested.");
	}

	private void ObserveGeneration2LocalDisconnect(double delta)
	{
		NetworkRuntimeClient? activeClient = client;
		if (activeClient is null)
		{
			Fail("Generation 2 local disconnect failed because the client is unavailable.");
			return;
		}

		if (!activeClient.TryPopEvent(out NetworkRuntimeEvent? clientEvent))
		{
			localDisconnectElapsedSeconds += delta;
			if (localDisconnectElapsedSeconds >= LocalDisconnectTimeoutSeconds)
			{
				Fail(
					$"Generation 2 local disconnect timed out after " +
					$"{LocalDisconnectTimeoutSeconds:0} seconds.");
			}
			return;
		}

		if (clientEvent is null ||
			clientEvent.Kind != NetworkRuntimeEventKind.TransportDisconnected ||
			clientEvent.DisconnectReason != NetworkRuntimeDisconnectReason.LocalRequested)
		{
			Fail(
				$"Generation 2 local disconnect received unexpected event: " +
				$"kind={clientEvent?.Kind}, reason={clientEvent?.DisconnectReason}.");
			return;
		}

		NetworkRuntimeStatus? transportStatus = clientEvent.TransportStatus;
		if (!transportStatus.HasValue || !transportStatus.Value.Succeeded)
		{
			string detail = transportStatus.HasValue
				? $"{transportStatus.Value.ErrorCode} (native={transportStatus.Value.NativeErrorCode})"
				: "missing transport status";
			Fail($"Generation 2 local disconnect returned unsuccessful status: {detail}.");
			return;
		}

		NetworkRuntimeStatus snapshotStatus =
			activeClient.CaptureSnapshot(out NetworkRuntimeClientSnapshot snapshot);
		if (!snapshotStatus.Succeeded ||
			snapshot.LifecycleState != NetworkRuntimeClientLifecycleState.Idle)
		{
			Fail(
				$"Generation 2 local disconnect expected Idle lifecycle: " +
				$"status={snapshotStatus.ErrorCode}, lifecycle={snapshot.LifecycleState}.");
			return;
		}

		state = FixtureState.Completed;
		GD.Print("Generation 2 local disconnect completed; lifecycle=Idle.");
		GetTree().Quit();
	}

	private bool TryDrainBoundaryResponse(NetworkRuntimeClient activeClient)
	{
		if (!activeClient.TryPopEvent(out NetworkRuntimeEvent? clientEvent) || clientEvent is null)
		{
			Fail("Boundary drain expected a response event, but the queue was empty.");
			return false;
		}

		if (clientEvent.Kind != NetworkRuntimeEventKind.PacketReceived ||
			clientEvent.PacketType != Pri57BenchmarkProtocol.ResponsePacketType)
		{
			Fail(
				$"Boundary drain received unexpected event: kind={clientEvent.Kind}, " +
				$"packetType={clientEvent.PacketType}.");
			return false;
		}

		if (clientEvent.Payload.Length != Pri57BenchmarkProtocol.CanonicalPayloadBytes)
		{
			Fail(
				$"Boundary response payload expected {Pri57BenchmarkProtocol.CanonicalPayloadBytes} bytes, " +
				$"actual {clientEvent.Payload.Length}.");
			return false;
		}

		ulong sequence = Pri57BenchmarkProtocol.DecodeSequence(clientEvent.Payload.Span);
		if (sequence == 0 || sequence > BoundaryResponseCount)
		{
			Fail($"Boundary response sequence is out of range: {sequence}.");
			return false;
		}

		int sequenceIndex = checked((int)sequence);
		if (receivedBoundarySequences[sequenceIndex])
		{
			Fail($"Boundary response sequence was duplicated: {sequence}.");
			return false;
		}

		receivedBoundarySequences[sequenceIndex] = true;
		return true;
	}

	private static ulong SteadyNanosecondsNow()
	{
		long timestamp = Stopwatch.GetTimestamp(); // 현재 시간

		// Frequency 는 1초에 tick 이 얼마나 증가하는지 나타내는 빈도수
		long wholeSeconds = timestamp / Stopwatch.Frequency;    // 초
		long remainingTicks = timestamp % Stopwatch.Frequency;  // 남은 tick

		// 나노초 변환
		return checked(
			((ulong)wholeSeconds * 1_000_000_000UL) +
			(((ulong)remainingTicks * 1_000_000_000UL) / (ulong)Stopwatch.Frequency));
	}

	private void Fail(string message)
	{
		state = FixtureState.Failed;
		GD.PushError(message);
	}
}
