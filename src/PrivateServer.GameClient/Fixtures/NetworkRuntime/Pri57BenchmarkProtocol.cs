using System;
using System.Buffers.Binary;

internal readonly struct Pri57BenchmarkResponse
{
	internal Pri57BenchmarkResponse(
		uint clientId,
		ulong sequence,
		ulong clientSendTimestampNanoseconds,
		ulong serverReceivedTimestampNanoseconds,
		ulong serverResponsePreparedTimestampNanoseconds)
	{
		ClientId = clientId;
		Sequence = sequence;
		ClientSendTimestampNanoseconds = clientSendTimestampNanoseconds;
		ServerReceivedTimestampNanoseconds = serverReceivedTimestampNanoseconds;
		ServerResponsePreparedTimestampNanoseconds = serverResponsePreparedTimestampNanoseconds;
	}

	internal uint ClientId { get; }
	internal ulong Sequence { get; }
	internal ulong ClientSendTimestampNanoseconds { get; }
	internal ulong ServerReceivedTimestampNanoseconds { get; }
	internal ulong ServerResponsePreparedTimestampNanoseconds { get; }
}

internal static class Pri57BenchmarkProtocol
{
	internal const uint RequestPacketType = 0x7F01;
	internal const uint ResponsePacketType = 0x7F02;
	internal const int CanonicalPayloadBytes = 64;

	private const ushort ProtocolVersion = 2;
	private const ushort EchoOperation = 1;
	private const int PayloadFieldBytes = 40;

	internal static byte[] EncodeRequest(
		uint clientId,
		ulong sequence,
		ulong clientSendTimestampNanoseconds)
	{
		byte[] payload = new byte[CanonicalPayloadBytes];
		Span<byte> payloadSpan = payload;

		BinaryPrimitives.WriteUInt16LittleEndian(payloadSpan[0..2], ProtocolVersion);
		BinaryPrimitives.WriteUInt16LittleEndian(payloadSpan[2..4], EchoOperation);
		BinaryPrimitives.WriteUInt32LittleEndian(payloadSpan[4..8], clientId);
		BinaryPrimitives.WriteUInt64LittleEndian(payloadSpan[8..16], sequence);
		BinaryPrimitives.WriteUInt64LittleEndian(payloadSpan[16..24], clientSendTimestampNanoseconds);

		for (int paddingIndex = 0; paddingIndex < CanonicalPayloadBytes - PayloadFieldBytes; ++paddingIndex)
		{
			payload[PayloadFieldBytes + paddingIndex] = checked((byte)paddingIndex);
		}

		return payload;
	}

	internal static ulong DecodeSequence(ReadOnlySpan<byte> payload)
	{
		if (payload.Length != CanonicalPayloadBytes)
		{
			throw new ArgumentException(
				$"Benchmark payload must contain {CanonicalPayloadBytes} bytes.",
				nameof(payload));
		}

		return BinaryPrimitives.ReadUInt64LittleEndian(payload[8..16]);
	}

	internal static bool TryDecodeResponse(
		ReadOnlySpan<byte> payload,
		out Pri57BenchmarkResponse response,
		out string error)
	{
		response = default;
		error = string.Empty;

		if (payload.Length != CanonicalPayloadBytes)
		{
			error = $"payload length expected {CanonicalPayloadBytes}, actual {payload.Length}";
			return false;
		}

		ushort protocolVersion = BinaryPrimitives.ReadUInt16LittleEndian(payload[0..2]);
		ushort operation = BinaryPrimitives.ReadUInt16LittleEndian(payload[2..4]);
		if (protocolVersion != ProtocolVersion || operation != EchoOperation)
		{
			error = $"unsupported protocol version={protocolVersion}, operation={operation}";
			return false;
		}

		for (int paddingIndex = 0; paddingIndex < CanonicalPayloadBytes - PayloadFieldBytes; ++paddingIndex)
		{
			byte expected = checked((byte)paddingIndex);
			byte actual = payload[PayloadFieldBytes + paddingIndex];
			if (actual != expected)
			{
				error = $"padding mismatch at index {paddingIndex}: expected {expected}, actual {actual}";
				return false;
			}
		}

		response = new Pri57BenchmarkResponse(
			BinaryPrimitives.ReadUInt32LittleEndian(payload[4..8]),
			BinaryPrimitives.ReadUInt64LittleEndian(payload[8..16]),
			BinaryPrimitives.ReadUInt64LittleEndian(payload[16..24]),
			BinaryPrimitives.ReadUInt64LittleEndian(payload[24..32]),
			BinaryPrimitives.ReadUInt64LittleEndian(payload[32..40]));
		return true;
	}
}
