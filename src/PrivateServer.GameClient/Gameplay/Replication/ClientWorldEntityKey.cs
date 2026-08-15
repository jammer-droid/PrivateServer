namespace PrivateServer.GameClient.Gameplay.Replication;

internal readonly record struct ClientWorldEntityKey(
    uint EntityId,
    uint Generation)
{
    internal bool IsValid => EntityId != 0 && Generation != 0;
}
