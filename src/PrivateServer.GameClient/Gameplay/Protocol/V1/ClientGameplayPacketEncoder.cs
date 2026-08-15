using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal readonly record struct EncodedClientGameplayPacket(
    uint PacketType,
    int PayloadByteCount);

internal static class ClientGameplayPacketEncoder
{
    internal static GameplayProtocolError Encode<TPacket>(
        TPacket packet,
        Span<byte> destination,
        out EncodedClientGameplayPacket encoded)
        where TPacket : struct, IClientGameplayPacket
    {
        int payloadByteCount = packet.PayloadByteCount;
        if (destination.Length < payloadByteCount)
        {
            encoded = default;
            return GameplayProtocolError.InvalidLength;
        }

        GameplayProtocolError encodeError = packet.Encode(destination[..payloadByteCount]);
        if (encodeError != GameplayProtocolError.Success)
        {
            encoded = default;
            return encodeError;
        }

        encoded = new EncodedClientGameplayPacket(packet.PacketType, payloadByteCount);
        return GameplayProtocolError.Success;
    }
}
