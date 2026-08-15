using System;
using System.Buffers.Binary;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal static class V1WireCodec
{
    internal const ushort PayloadVersion = 1;

    internal static GameplayProtocolError ValidateFixedPayload(
        ReadOnlySpan<byte> payload,
        int expectedPayloadBytes)
    {
        if (payload.Length != expectedPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (ReadU16(0, payload) != PayloadVersion)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        return GameplayProtocolError.Success;
    }

    internal static void WriteVersion(Span<byte> output)
    {
        WriteU16(PayloadVersion, 0, output);
    }

    internal static void WriteU16(ushort value, int offset, Span<byte> output)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(output.Slice(offset, sizeof(ushort)), value);
    }

    internal static ushort ReadU16(int offset, ReadOnlySpan<byte> payload)
    {
        return BinaryPrimitives.ReadUInt16LittleEndian(payload.Slice(offset, sizeof(ushort)));
    }

    internal static void WriteU32(uint value, int offset, Span<byte> output)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(output.Slice(offset, sizeof(uint)), value);
    }

    internal static uint ReadU32(int offset, ReadOnlySpan<byte> payload)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, sizeof(uint)));
    }

    internal static void WriteI16(short value, int offset, Span<byte> output)
    {
        BinaryPrimitives.WriteInt16LittleEndian(output.Slice(offset, sizeof(short)), value);
    }

    internal static short ReadI16(int offset, ReadOnlySpan<byte> payload)
    {
        return BinaryPrimitives.ReadInt16LittleEndian(payload.Slice(offset, sizeof(short)));
    }

    internal static void WriteF32(float value, int offset, Span<byte> output)
    {
        float normalized = value == 0.0f ? 0.0f : value;
        WriteU32(BitConverter.SingleToUInt32Bits(normalized), offset, output);
    }

    internal static float ReadF32(int offset, ReadOnlySpan<byte> payload)
    {
        float value = BitConverter.UInt32BitsToSingle(ReadU32(offset, payload));
        return value == 0.0f ? 0.0f : value;
    }
}
