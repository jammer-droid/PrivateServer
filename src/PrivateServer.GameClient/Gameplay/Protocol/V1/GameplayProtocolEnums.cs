namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal enum EntityKind : ushort
{
    Invalid = 0,
    Player = 1,
    Resource = 2,
    StaticObstacle = 3,
}

internal enum ShapeKind : ushort
{
    Invalid = 0,
    Circle = 1,
}

internal enum EntityRemoveReason : ushort
{
    Invalid = 0,
    LeftAoi = 1,
    Destroyed = 2,
    Collected = 3,
    SessionClosed = 4,
    RoundReset = 5,
}

internal enum RoundPhase : ushort
{
    Invalid = 0,
    Waiting = 1,
    Running = 2,
    Ended = 3,
}
