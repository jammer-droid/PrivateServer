using System;
using System.Collections.Generic;
using System.Numerics;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Prediction;
using PrivateServer.GameClient.Gameplay.Protocol.V1;

using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using ControlledEntityBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityBodySample;
using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using TurnStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.TurnState;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class ControlledEntityPredictionTests
{
    [TestMethod]
    public void PredictsNormalizedUnblockedMovementAtFixedTickRate()
    {
        ControlledEntityPrediction prediction = MakePrediction(
            MakeReady(tickRateHz: 1),
            MakeControlledSpawn());
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.SetInput(3.0f, 4.0f));

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictFixedTick(Array.Empty<ClientStaticObstacle>()));

        ControlledEntityPredictionSnapshot snapshot = prediction.Snapshot;
        Assert.AreEqual(6.0f, snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(8.0f, snapshot.LogicalPosition.Y, 0.0001f);
        Assert.AreEqual(6.0f, snapshot.Velocity.X, 0.0001f);
        Assert.AreEqual(8.0f, snapshot.Velocity.Y, 0.0001f);
    }

    [TestMethod]
    public void SweepsToArenaBoundaryAndSlidesAlongIt()
    {
        WorldReady ready = MakeReady(
            tickRateHz: 1,
            arenaMaxX: 5.0f);
        ControlledEntityPrediction prediction =
            MakePrediction(ready, MakeControlledSpawn());
        prediction.SetInput(0.8f, 0.6f);

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictFixedTick(Array.Empty<ClientStaticObstacle>()));

        ControlledEntityPredictionSnapshot snapshot = prediction.Snapshot;
        Assert.AreEqual(4.0f, snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(6.0f, snapshot.LogicalPosition.Y, 0.0001f);
        Assert.AreEqual(4.0f, snapshot.Velocity.X, 0.0001f);
        Assert.AreEqual(6.0f, snapshot.Velocity.Y, 0.0001f);
    }

    [TestMethod]
    public void SweepsAgainstStaticCircleWithoutTunneling()
    {
        ControlledEntityPrediction prediction = MakePrediction(
            MakeReady(tickRateHz: 1),
            MakeControlledSpawn());
        prediction.SetInput(1.0f, 0.0f);
        ClientStaticObstacle obstacle = new ClientStaticObstacle(
            20,
            1,
            new Vector2(6.0f, 0.0f),
            1.0f);

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictFixedTick(new[] { obstacle }));

        ControlledEntityPredictionSnapshot snapshot = prediction.Snapshot;
        Assert.AreEqual(4.0f, snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(0.0f, snapshot.LogicalPosition.Y, 0.0001f);
    }

    [TestMethod]
    public void AuthoritativeCorrectionChangesLogicalStateImmediatelyAndDecaysRenderOffset()
    {
        ControlledEntityPrediction prediction = MakePrediction(
            MakeReady(tickRateHz: 10),
            MakeControlledSpawn());
        prediction.SetInput(1.0f, 0.0f);
        prediction.PredictFixedTick(Array.Empty<ClientStaticObstacle>());
        Assert.AreEqual(1.0f, prediction.Snapshot.RenderPosition.X, 0.0001f);

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.ApplyAuthoritative(
                new ControlledEntityState(10, 3, 0.5f, 0, 0, 0, 0)));

        Assert.AreEqual(0.5f, prediction.Snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(1.0f, prediction.Snapshot.RenderPosition.X, 0.0001f);
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.AdvanceRenderCorrection(0.10f));
        Assert.AreEqual(0.25f, prediction.Snapshot.CorrectionOffset.X, 0.0001f);
        Assert.AreEqual(0.75f, prediction.Snapshot.RenderPosition.X, 0.0001f);

        prediction.ApplyAuthoritative(
            new ControlledEntityState(11, 3, 0.73f, 0, 0, 0, 0));
        Assert.AreEqual(Vector2.Zero, prediction.Snapshot.CorrectionOffset);
        Assert.AreEqual(0.73f, prediction.Snapshot.RenderPosition.X, 0.0001f);

        prediction.ApplyAuthoritative(
            new ControlledEntityState(12, 3, -5.0f, 0, 0, 0, 0));
        Assert.AreEqual(Vector2.Zero, prediction.Snapshot.CorrectionOffset);
        Assert.AreEqual(-5.0f, prediction.Snapshot.RenderPosition.X, 0.0001f);
    }

    [TestMethod]
    public void StaleGenerationDoesNotMutatePrediction()
    {
        ControlledEntityPrediction prediction = MakePrediction(
            MakeReady(tickRateHz: 10),
            MakeControlledSpawn());
        ControlledEntityPredictionSnapshot before = prediction.Snapshot;

        Assert.AreEqual(
            ControlledEntityPredictionError.StaleGeneration,
            prediction.ApplyAuthoritative(
                new ControlledEntityState(10, 2, 100, 100, 0, 0, 0)));
        Assert.AreEqual(before, prediction.Snapshot);
    }

    [TestMethod]
    public void V2ControlPredictionTurnsAndUsesOnlyAuthoritativeBoostState()
    {
        ControlledEntityPrediction prediction = MakePrediction(
            MakeReady(tickRateHz: 10),
            MakeControlledSpawn());
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.ApplyAuthoritative(MakeV2State(BoostStateV2.Off)));
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.SetControlTurnState(TurnStateV2.Left));
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictControlFrame(0.5f));

        Assert.AreEqual(MathF.PI / 2.0f, prediction.Snapshot.AngleRadians, 0.0001f);
        Assert.AreEqual(0.0f, prediction.Snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(2.5f, prediction.Snapshot.LogicalPosition.Y, 0.0001f);

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.SetControlTurnState(TurnStateV2.Straight));
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictControlFrame(0.2f));
        Assert.AreEqual(MathF.PI / 2.0f, prediction.Snapshot.AngleRadians, 0.0001f);
        Assert.AreEqual(0.0f, prediction.Snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(3.5f, prediction.Snapshot.LogicalPosition.Y, 0.0001f);

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.ApplyAuthoritative(MakeV2State(BoostStateV2.On)));
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.SetControlTurnState(TurnStateV2.Straight));
        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictControlFrame(0.4f));
        Assert.AreEqual(3.0f, prediction.Snapshot.LogicalPosition.X, 0.0001f);
        Assert.AreEqual(0.0f, prediction.Snapshot.LogicalPosition.Y, 0.0001f);
    }

    [TestMethod]
    public void PredictionUsesInjectedPhysicsAdapterContract()
    {
        FakePredictionPhysics physics = new FakePredictionPhysics(
            new ClientPredictionPhysicsResult(
                new Vector2(7.0f, 8.0f),
                new Vector2(1.0f, 2.0f),
                false));
        ControlledEntityPrediction prediction = new ControlledEntityPrediction(
            MakeControlledSpawn(),
            MakeReady(tickRateHz: 10),
            physics: physics);
        prediction.SetInput(1.0f, 0.0f);

        Assert.AreEqual(
            ControlledEntityPredictionError.None,
            prediction.PredictFixedTick(Array.Empty<ClientStaticObstacle>()));

        Assert.IsNotNull(physics.LastStep);
        Assert.AreEqual(Vector2.Zero, physics.LastStep.Value.StartPosition);
        Assert.AreEqual(new Vector2(1.0f, 0.0f), physics.LastStep.Value.DesiredDisplacement);
        Assert.AreEqual(new Vector2(7.0f, 8.0f), prediction.Snapshot.LogicalPosition);
        Assert.AreEqual(new Vector2(1.0f, 2.0f), prediction.Snapshot.Velocity);
    }

    private static ControlledEntityPrediction MakePrediction(
        WorldReady ready,
        EntitySpawn controlledSpawn)
    {
        return new ControlledEntityPrediction(controlledSpawn, ready);
    }

    private static EntitySpawn MakeControlledSpawn()
    {
        return new EntitySpawn(
            1,
            10,
            3,
            EntityKind.Player,
            1,
            ShapeKind.Circle,
            1.0f,
            10.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f);
    }

    private static WorldReady MakeReady(
        uint tickRateHz,
        float arenaMaxX = 100.0f)
    {
        return new WorldReady(
            20,
            10,
            3,
            1,
            tickRateHz,
            1,
            2,
            -100.0f,
            -100.0f,
            arenaMaxX,
            100.0f);
    }

    private static ControlledEntityStateV2 MakeV2State(BoostStateV2 boostState)
    {
        return new ControlledEntityStateV2(
            10,
            3,
            0,
            0.0f,
            0.0f,
            0.0f,
            2.0f,
            1,
            boostState,
            new[] { new ControlledEntityBodySampleV2(0.0f, 0.0f) });
    }

    private sealed class FakePredictionPhysics : IClientPredictionPhysics
    {
        private readonly ClientPredictionPhysicsResult result;

        internal FakePredictionPhysics(ClientPredictionPhysicsResult result)
        {
            this.result = result;
        }

        internal ClientPredictionPhysicsStep? LastStep { get; private set; }

        public ClientPredictionPhysicsError Resolve(
            ClientPredictionPhysicsStep step,
            out ClientPredictionPhysicsResult resolved)
        {
            LastStep = step;
            resolved = result;
            return ClientPredictionPhysicsError.None;
        }
    }
}
