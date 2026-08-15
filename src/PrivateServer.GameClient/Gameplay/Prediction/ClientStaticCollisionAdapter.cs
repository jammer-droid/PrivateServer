using System;
using System.Collections.Generic;
using System.Numerics;

namespace PrivateServer.GameClient.Gameplay.Prediction;

internal readonly record struct ClientStaticCollisionConfig(
    int MaxContactsPerTick,
    float CollisionEpsilon,
    float ContactTolerance)
{
    internal static ClientStaticCollisionConfig Default =>
        new ClientStaticCollisionConfig(4, 0.0001f, 0.0001f);
}

internal sealed class ClientStaticCollisionAdapter : IClientPredictionPhysics
{
    private readonly ClientStaticCollisionConfig config;

    private enum ColliderKind
    {
        Arena,
        StaticObstacle,
    }

    private enum ArenaBoundary
    {
        Left,
        Bottom,
        Right,
        Top,
    }

    private readonly record struct SweepHit(
        float TimeOfImpact,
        Vector2 Normal,
        ColliderKind Kind,
        ArenaBoundary Boundary,
        uint EntityId,
        uint Generation);

    internal ClientStaticCollisionAdapter(ClientStaticCollisionConfig? config = null)
    {
        ClientStaticCollisionConfig selectedConfig =
            config ?? ClientStaticCollisionConfig.Default;
        if (!IsValid(selectedConfig))
        {
            throw new ArgumentOutOfRangeException(
                nameof(config),
                "Client static collision configuration is invalid.");
        }
        this.config = selectedConfig;
    }

    public ClientPredictionPhysicsError Resolve(
        ClientPredictionPhysicsStep step,
        out ClientPredictionPhysicsResult result)
    {
        result = default;
        Vector2 startPosition = step.StartPosition;
        float movingRadius = step.MovingRadius;
        Vector2 desiredDisplacement = step.DesiredDisplacement;
        float fixedDeltaSeconds = step.FixedDeltaSeconds;
        ClientArenaBounds arena = step.Arena;
        IReadOnlyList<ClientStaticObstacle> obstacles = step.StaticObstacles;
        if (!IsFinite(startPosition) ||
            !IsFinite(desiredDisplacement) ||
            !float.IsFinite(movingRadius) ||
            movingRadius <= 0.0f ||
            !float.IsFinite(fixedDeltaSeconds) ||
            fixedDeltaSeconds <= 0.0f ||
            !IsValid(arena) ||
            obstacles is null ||
            !IsValid(config))
        {
            return ClientPredictionPhysicsError.InvalidArgument;
        }

        if (HasInitialArenaPenetration(
                startPosition,
                movingRadius,
                arena,
                config.ContactTolerance))
        {
            return ClientPredictionPhysicsError.InitialPenetration;
        }

        for (int index = 0; index < obstacles.Count; ++index)
        {
            ClientStaticObstacle obstacle = obstacles[index];
            if (!IsValid(obstacle))
            {
                return ClientPredictionPhysicsError.InvalidArgument;
            }
            if (HasInitialCirclePenetration(
                    startPosition,
                    movingRadius,
                    obstacle.Position,
                    obstacle.Radius,
                    config.ContactTolerance))
            {
                return ClientPredictionPhysicsError.InitialPenetration;
            }
        }

        List<SweepHit> candidates = new List<SweepHit>(obstacles.Count + 2);
        Vector2 currentPosition = startPosition;
        Vector2 remainingDisplacement = desiredDisplacement;
        float epsilonSquared = config.CollisionEpsilon * config.CollisionEpsilon;

        for (int contactOrdinal = 0;
             contactOrdinal < config.MaxContactsPerTick;
             ++contactOrdinal)
        {
            if (remainingDisplacement.LengthSquared() <= epsilonSquared)
            {
                remainingDisplacement = Vector2.Zero;
                break;
            }

            candidates.Clear();
            SweepArena(
                currentPosition,
                remainingDisplacement,
                movingRadius,
                arena,
                candidates);
            for (int index = 0; index < obstacles.Count; ++index)
            {
                ClientStaticObstacle obstacle = obstacles[index];
                if (!SweptAabbOverlapsCircle(
                        currentPosition,
                        remainingDisplacement,
                        movingRadius,
                        obstacle.Position,
                        obstacle.Radius))
                {
                    continue;
                }

                SweepStaticCircle(
                    currentPosition,
                    remainingDisplacement,
                    movingRadius,
                    obstacle,
                    candidates);
            }

            if (!TrySelectStableHit(
                    candidates,
                    config.ContactTolerance,
                    out SweepHit earliestHit))
            {
                currentPosition += remainingDisplacement;
                remainingDisplacement = Vector2.Zero;
                break;
            }

            currentPosition += remainingDisplacement * earliestHit.TimeOfImpact;
            remainingDisplacement *= 1.0f - earliestHit.TimeOfImpact;
            float inwardDistance = Vector2.Dot(remainingDisplacement, earliestHit.Normal);
            if (inwardDistance < 0.0f)
            {
                remainingDisplacement -= earliestHit.Normal * inwardDistance;
            }
        }

        bool contactCapReached = remainingDisplacement.LengthSquared() > epsilonSquared;
        Vector2 velocity = contactCapReached
            ? Vector2.Zero
            : (currentPosition - startPosition) / fixedDeltaSeconds;
        result = new ClientPredictionPhysicsResult(
            currentPosition,
            velocity,
            contactCapReached);
        return ClientPredictionPhysicsError.None;
    }

    private static bool IsValid(ClientArenaBounds arena)
    {
        return float.IsFinite(arena.MinimumX) &&
            float.IsFinite(arena.MinimumY) &&
            float.IsFinite(arena.MaximumX) &&
            float.IsFinite(arena.MaximumY) &&
            arena.MinimumX < arena.MaximumX &&
            arena.MinimumY < arena.MaximumY;
    }

    private static bool IsValid(ClientStaticObstacle obstacle)
    {
        return obstacle.EntityId != 0 &&
            obstacle.Generation != 0 &&
            IsFinite(obstacle.Position) &&
            float.IsFinite(obstacle.Radius) &&
            obstacle.Radius > 0.0f;
    }

    private static bool IsValid(ClientStaticCollisionConfig config)
    {
        return config.MaxContactsPerTick > 0 &&
            float.IsFinite(config.CollisionEpsilon) &&
            config.CollisionEpsilon > 0.0f &&
            float.IsFinite(config.ContactTolerance) &&
            config.ContactTolerance > 0.0f;
    }

    private static bool IsFinite(Vector2 value)
    {
        return float.IsFinite(value.X) && float.IsFinite(value.Y);
    }

    private static bool HasInitialArenaPenetration(
        Vector2 center,
        float radius,
        ClientArenaBounds arena,
        float contactTolerance)
    {
        return center.X - radius < arena.MinimumX - contactTolerance ||
            center.X + radius > arena.MaximumX + contactTolerance ||
            center.Y - radius < arena.MinimumY - contactTolerance ||
            center.Y + radius > arena.MaximumY + contactTolerance;
    }

    private static bool HasInitialCirclePenetration(
        Vector2 movingCenter,
        float movingRadius,
        Vector2 staticCenter,
        float staticRadius,
        float contactTolerance)
    {
        Vector2 difference = movingCenter - staticCenter;
        float minimumDistance = movingRadius + staticRadius - contactTolerance;
        return minimumDistance > 0.0f &&
            difference.LengthSquared() < minimumDistance * minimumDistance;
    }

    private static bool SweptAabbOverlapsCircle(
        Vector2 startCenter,
        Vector2 displacement,
        float movingRadius,
        Vector2 staticCenter,
        float staticRadius)
    {
        Vector2 endCenter = startCenter + displacement;
        float movingMinimumX = MathF.Min(startCenter.X, endCenter.X) - movingRadius;
        float movingMaximumX = MathF.Max(startCenter.X, endCenter.X) + movingRadius;
        float movingMinimumY = MathF.Min(startCenter.Y, endCenter.Y) - movingRadius;
        float movingMaximumY = MathF.Max(startCenter.Y, endCenter.Y) + movingRadius;
        float staticMinimumX = staticCenter.X - staticRadius;
        float staticMaximumX = staticCenter.X + staticRadius;
        float staticMinimumY = staticCenter.Y - staticRadius;
        float staticMaximumY = staticCenter.Y + staticRadius;

        return movingMinimumX <= staticMaximumX &&
            movingMaximumX >= staticMinimumX &&
            movingMinimumY <= staticMaximumY &&
            movingMaximumY >= staticMinimumY;
    }

    private static void SweepArena(
        Vector2 startCenter,
        Vector2 displacement,
        float radius,
        ClientArenaBounds arena,
        List<SweepHit> hits)
    {
        if (displacement.X < 0.0f)
        {
            SweepArenaBoundary(
                displacement.X,
                startCenter.X,
                arena.MinimumX + radius,
                Vector2.UnitX,
                ArenaBoundary.Left,
                hits);
        }
        else if (displacement.X > 0.0f)
        {
            SweepArenaBoundary(
                displacement.X,
                startCenter.X,
                arena.MaximumX - radius,
                -Vector2.UnitX,
                ArenaBoundary.Right,
                hits);
        }

        if (displacement.Y < 0.0f)
        {
            SweepArenaBoundary(
                displacement.Y,
                startCenter.Y,
                arena.MinimumY + radius,
                Vector2.UnitY,
                ArenaBoundary.Bottom,
                hits);
        }
        else if (displacement.Y > 0.0f)
        {
            SweepArenaBoundary(
                displacement.Y,
                startCenter.Y,
                arena.MaximumY - radius,
                -Vector2.UnitY,
                ArenaBoundary.Top,
                hits);
        }
    }

    private static void SweepArenaBoundary(
        float displacementComponent,
        float startComponent,
        float allowedComponent,
        Vector2 normal,
        ArenaBoundary boundary,
        List<SweepHit> hits)
    {
        float timeOfImpact =
            (allowedComponent - startComponent) / displacementComponent;
        if (timeOfImpact < 0.0f || timeOfImpact > 1.0f)
        {
            return;
        }

        hits.Add(new SweepHit(
            timeOfImpact,
            normal,
            ColliderKind.Arena,
            boundary,
            0,
            0));
    }

    private static void SweepStaticCircle(
        Vector2 startCenter,
        Vector2 displacement,
        float movingRadius,
        ClientStaticObstacle obstacle,
        List<SweepHit> hits)
    {
        float combinedRadius = movingRadius + obstacle.Radius;
        Vector2 relativeStart = startCenter - obstacle.Position;
        float a = displacement.LengthSquared();
        float b = Vector2.Dot(relativeStart, displacement);
        float c = relativeStart.LengthSquared() - combinedRadius * combinedRadius;
        if (a <= 0.0f || b >= 0.0f)
        {
            return;
        }

        float discriminant = b * b - a * c;
        if (discriminant < 0.0f)
        {
            return;
        }

        float timeOfImpact = (-b - MathF.Sqrt(discriminant)) / a;
        if (timeOfImpact < 0.0f || timeOfImpact > 1.0f)
        {
            return;
        }

        Vector2 hitCenter = startCenter + displacement * timeOfImpact;
        Vector2 normal = hitCenter - obstacle.Position;
        if (normal.LengthSquared() <= 0.0f)
        {
            return;
        }
        normal = Vector2.Normalize(normal);
        hits.Add(new SweepHit(
            timeOfImpact,
            normal,
            ColliderKind.StaticObstacle,
            default,
            obstacle.EntityId,
            obstacle.Generation));
    }

    private static bool TrySelectStableHit(
        IReadOnlyList<SweepHit> candidates,
        float contactTolerance,
        out SweepHit selected)
    {
        selected = default;
        if (candidates.Count == 0)
        {
            return false;
        }

        float minimumTimeOfImpact = candidates[0].TimeOfImpact;
        for (int index = 1; index < candidates.Count; ++index)
        {
            minimumTimeOfImpact = MathF.Min(
                minimumTimeOfImpact,
                candidates[index].TimeOfImpact);
        }

        float tiedMaximumTimeOfImpact = minimumTimeOfImpact + contactTolerance;
        bool hasSelected = false;
        for (int index = 0; index < candidates.Count; ++index)
        {
            SweepHit candidate = candidates[index];
            if (candidate.TimeOfImpact > tiedMaximumTimeOfImpact)
            {
                continue;
            }
            if (!hasSelected || ColliderKeyLess(candidate, selected))
            {
                selected = candidate;
                hasSelected = true;
            }
        }

        return hasSelected;
    }

    private static bool ColliderKeyLess(SweepHit left, SweepHit right)
    {
        if (left.Kind != right.Kind)
        {
            return left.Kind < right.Kind;
        }
        if (left.Kind == ColliderKind.Arena)
        {
            return left.Boundary < right.Boundary;
        }
        if (left.EntityId != right.EntityId)
        {
            return left.EntityId < right.EntityId;
        }

        return left.Generation < right.Generation;
    }
}
