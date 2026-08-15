namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal abstract record ServerGameplayPacket
{
    internal abstract uint PacketType { get; }
}

internal abstract record ServerGameplayPacketV1 : ServerGameplayPacket
{
}
