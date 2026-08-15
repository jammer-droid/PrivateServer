using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal interface IClientGameplayPacket
{
    uint PacketType { get; }
    int PayloadByteCount { get; }

    GameplayProtocolError Encode(Span<byte> output);
}

internal interface IClientGameplayPacketV1 : IClientGameplayPacket
{
}
