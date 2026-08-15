using System.Collections.Generic;
using System.Numerics;

namespace PrivateServer.GameClient.Gameplay.Prediction;

internal readonly record struct ClientArenaBounds(
    float MinimumX,
    float MinimumY,
    float MaximumX,
    float MaximumY);

internal readonly record struct ClientStaticObstacle(
    uint EntityId,
    uint Generation,
    Vector2 Position,
    float Radius);

internal readonly record struct ClientPredictionPhysicsStep(
    Vector2 StartPosition,
    float MovingRadius,
    Vector2 DesiredDisplacement,
    float FixedDeltaSeconds,
    ClientArenaBounds Arena,
    IReadOnlyList<ClientStaticObstacle> StaticObstacles);

internal enum ClientPredictionPhysicsError
{
    None,
    InvalidArgument,
    InitialPenetration,
}

internal readonly record struct ClientPredictionPhysicsResult(
    Vector2 Position,
    Vector2 Velocity,
    bool ContactCapReached);

internal interface IClientPredictionPhysics
{
    ClientPredictionPhysicsError Resolve(
        ClientPredictionPhysicsStep step,
        out ClientPredictionPhysicsResult result);
}
