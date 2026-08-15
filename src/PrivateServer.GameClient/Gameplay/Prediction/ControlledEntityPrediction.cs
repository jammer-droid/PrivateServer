using PrivateServer.GameClient.Gameplay.Protocol.V1;
using System;
using System.Collections.Generic;
using System.Numerics;

using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using TurnStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.TurnState;

namespace PrivateServer.GameClient.Gameplay.Prediction;

internal readonly record struct ControlledEntityPredictionConfig(
    float ReconciliationTolerance,
    float HardSnapDistance,
    float CorrectionHalfLifeSeconds)
{
    internal static ControlledEntityPredictionConfig Default =>
        new ControlledEntityPredictionConfig(
            0.05f,
            2.0f,
            0.10f);
}

internal readonly record struct ControlledEntityPredictionSnapshot(
    uint Generation,
    Vector2 LogicalPosition,
    Vector2 Velocity,
    float AngleRadians,
    Vector2 RenderPosition,
    Vector2 CorrectionOffset,
    Vector2 CurrentInput);

internal enum ControlledEntityPredictionError
{
    None,
    InvalidArgument,
    StaleGeneration,
    CollisionFailure,
}

internal sealed class ControlledEntityPrediction
{
    internal const float ControlBaseSpeed = 5.0f;
    internal const float ControlBoostSpeed = 7.5f;
    internal const float ControlAngularSpeedRadiansPerSecond = MathF.PI;

    private float radius;
    private readonly float maxMoveSpeed;
    private readonly float fixedDeltaSeconds;
    private readonly ClientArenaBounds arena;
    private readonly ControlledEntityPredictionConfig config;
    private readonly IClientPredictionPhysics physics;
    private Vector2 logicalPosition;
    private Vector2 velocity;
    private float angleRadians;
    private Vector2 correctionOffset;
    private Vector2 currentInput;
    private TurnStateV2 controlTurnState = TurnStateV2.Straight;
    private bool authoritativeBoostActive;

    internal ControlledEntityPrediction(
        EntitySpawn controlledSpawn,
        WorldReady ready,
        ControlledEntityPredictionConfig? config = null,
        IClientPredictionPhysics? physics = null)
    {
        ArgumentNullException.ThrowIfNull(controlledSpawn);
        ArgumentNullException.ThrowIfNull(ready);
        if (controlledSpawn.EntityKind != EntityKind.Player ||
            controlledSpawn.EntityId != ready.ControlledEntityId ||
            controlledSpawn.Generation != ready.ControlledEntityGeneration)
        {
            throw new ArgumentException(
                "The controlled spawn must match the WorldReady Player identity.",
                nameof(controlledSpawn));
        }

        ControlledEntityPredictionConfig selectedConfig =
            config ?? ControlledEntityPredictionConfig.Default;
        ValidateConfig(selectedConfig);

        Generation = controlledSpawn.Generation;
        radius = controlledSpawn.PrimaryCircleRadius;
        maxMoveSpeed = controlledSpawn.MaxMoveSpeed;
        fixedDeltaSeconds = 1.0f / ready.TickRateHz;
        arena = new ClientArenaBounds(
            ready.ArenaMinX,
            ready.ArenaMinY,
            ready.ArenaMaxX,
            ready.ArenaMaxY);
        this.config = selectedConfig;
        this.physics = physics ?? new ClientStaticCollisionAdapter();
        logicalPosition = new Vector2(
            controlledSpawn.PositionX,
            controlledSpawn.PositionY);
        velocity = new Vector2(
            controlledSpawn.VelocityX,
            controlledSpawn.VelocityY);
        angleRadians = controlledSpawn.AngleRadians;
    }

    internal uint Generation { get; }
    internal float Radius => radius;
    internal Vector2 CurrentInput => currentInput;
    internal ControlledEntityPredictionSnapshot Snapshot =>
        new ControlledEntityPredictionSnapshot(
            Generation,
            logicalPosition,
            velocity,
            angleRadians,
            logicalPosition + correctionOffset,
            correctionOffset,
            currentInput);

    internal ControlledEntityPredictionError SetInput(float inputX, float inputY)
    {
        if (!float.IsFinite(inputX) || !float.IsFinite(inputY))
        {
            return ControlledEntityPredictionError.InvalidArgument;
        }

        Vector2 input = new Vector2(inputX, inputY);
        if (input.LengthSquared() > 1.0f)
        {
            input = Vector2.Normalize(input);
        }
        currentInput = input;
        return ControlledEntityPredictionError.None;
    }

    internal ControlledEntityPredictionError PredictFixedTick(
        IReadOnlyList<ClientStaticObstacle> obstacles)
    {
        Vector2 desiredVelocity = currentInput * maxMoveSpeed;
        Vector2 desiredDisplacement = desiredVelocity * fixedDeltaSeconds;
        ClientPredictionPhysicsStep step = new ClientPredictionPhysicsStep(
            logicalPosition,
            radius,
            desiredDisplacement,
            fixedDeltaSeconds,
            arena,
            obstacles);
        // arena bound, obstacle check, sweep-and-slide(max 4)
        ClientPredictionPhysicsError collisionError = physics.Resolve(
            step,
            out ClientPredictionPhysicsResult collisionResult);
        if (collisionError != ClientPredictionPhysicsError.None)
        {
            return ControlledEntityPredictionError.CollisionFailure;
        }

        logicalPosition = collisionResult.Position;
        velocity = collisionResult.Velocity;
        return ControlledEntityPredictionError.None;
    }

    // 서버 결과 반영
    internal ControlledEntityPredictionError ApplyAuthoritative(
        ControlledEntityState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (state.ControlledEntityGeneration != Generation)
        {
            return ControlledEntityPredictionError.StaleGeneration;
        }

        Vector2 previousRenderPosition = logicalPosition + correctionOffset;            // 이전 렌더링 포지션
        Vector2 authoritativePosition = new Vector2(state.PositionX, state.PositionY);  // 서버 검증 포지션
        float correctionDistance = Vector2.Distance(
            previousRenderPosition,
            authoritativePosition);

        // 서버 값으로 갱신
        logicalPosition = authoritativePosition;
        velocity = new Vector2(state.VelocityX, state.VelocityY);
        angleRadians = state.AngleRadians;
        if (correctionDistance <= config.ReconciliationTolerance ||
            correctionDistance > config.HardSnapDistance)
        {
            correctionOffset = Vector2.Zero;
        }
        else
        {
            correctionOffset = previousRenderPosition - logicalPosition; // 오차 갱신
        }

        return ControlledEntityPredictionError.None;
    }

    internal ControlledEntityPredictionError SetControlTurnState(TurnStateV2 turnState)
    {
        if (turnState != TurnStateV2.Straight &&
            turnState != TurnStateV2.Left &&
            turnState != TurnStateV2.Right)
        {
            return ControlledEntityPredictionError.InvalidArgument;
        }

        controlTurnState = turnState;
        return ControlledEntityPredictionError.None;
    }

    internal ControlledEntityPredictionError PredictControlFrame(float deltaSeconds)
    {
        if (!float.IsFinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            return ControlledEntityPredictionError.InvalidArgument;
        }
        if (deltaSeconds == 0.0f)
        {
            return ControlledEntityPredictionError.None;
        }

        float turnSign = controlTurnState switch
        {
            TurnStateV2.Left => 1.0f,
            TurnStateV2.Right => -1.0f,
            _ => 0.0f,
        };
        angleRadians = NormalizeHeading(
            angleRadians + turnSign * ControlAngularSpeedRadiansPerSecond * deltaSeconds);
        float speed = authoritativeBoostActive ? ControlBoostSpeed : ControlBaseSpeed;
        velocity = new Vector2(MathF.Cos(angleRadians), MathF.Sin(angleRadians)) * speed;
        logicalPosition += velocity * deltaSeconds;
        return ControlledEntityPredictionError.None;
    }

    internal ControlledEntityPredictionError ApplyAuthoritative(
        ControlledEntityStateV2 state)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (state.ControlledEntityGeneration != Generation)
        {
            return ControlledEntityPredictionError.StaleGeneration;
        }

        Vector2 previousRenderPosition = logicalPosition + correctionOffset;
        Vector2 authoritativePosition = new Vector2(
            state.HeadPositionX,
            state.HeadPositionY);
        float correctionDistance = Vector2.Distance(
            previousRenderPosition,
            authoritativePosition);

        logicalPosition = authoritativePosition;
        angleRadians = state.HeadingRadians;
        radius = state.Diameter * 0.5f;
        authoritativeBoostActive = state.BoostState == BoostStateV2.On;
        if (correctionDistance <= config.ReconciliationTolerance ||
            correctionDistance > config.HardSnapDistance)
        {
            correctionOffset = Vector2.Zero;
        }
        else
        {
            correctionOffset = previousRenderPosition - logicalPosition;
        }

        return ControlledEntityPredictionError.None;
    }

    private static float NormalizeHeading(float headingRadians)
    {
        float normalized = (headingRadians + MathF.PI) % MathF.Tau;
        if (normalized < 0.0f)
        {
            normalized += MathF.Tau;
        }
        return normalized - MathF.PI;
    }

    // 이미 계산된 render offset 을 시간에 따라 감소
    internal ControlledEntityPredictionError AdvanceRenderCorrection(
        float deltaSeconds)
    {
        if (!float.IsFinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            return ControlledEntityPredictionError.InvalidArgument;
        }
        if (deltaSeconds == 0.0f || correctionOffset == Vector2.Zero)
        {
            return ControlledEntityPredictionError.None;
        }

        // 반감기 기반으로 매 프레임 offset 을 줄임
        // 신규 offset = 기존 offset * decay
        // deltaSeconds = 이번 프레임 경과 시간, HalfLife 반감기 설정값
        float decay = MathF.Pow(
            0.5f,
            deltaSeconds / config.CorrectionHalfLifeSeconds);
        correctionOffset *= decay;
        if (correctionOffset.LengthSquared() <=
            config.ReconciliationTolerance * config.ReconciliationTolerance)
        {
            correctionOffset = Vector2.Zero;
        }

        return ControlledEntityPredictionError.None;
    }

    private static void ValidateConfig(ControlledEntityPredictionConfig config)
    {
        if (!float.IsFinite(config.ReconciliationTolerance) ||
            config.ReconciliationTolerance < 0.0f ||
            !float.IsFinite(config.HardSnapDistance) ||
            config.HardSnapDistance <= config.ReconciliationTolerance ||
            !float.IsFinite(config.CorrectionHalfLifeSeconds) ||
            config.CorrectionHalfLifeSeconds <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(
                nameof(config),
                "Controlled prediction configuration is invalid.");
        }
    }
}
