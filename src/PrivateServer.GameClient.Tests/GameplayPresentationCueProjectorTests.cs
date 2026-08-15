using Microsoft.VisualStudio.TestTools.UnitTesting;
using PrivateServer.GameClient.Gameplay.Presentation;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Replication;
using System.Collections.Generic;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayPresentationCueProjectorTests
{
    [TestMethod]
    public void ResourceDestroyedRemovalDoesNotProducePresentationCue()
    {
        Assert.IsFalse(GameplayPresentationCueProjector.ShouldPresentRemoval(
            EntityKind.Resource,
            EntityRemoveReason.Destroyed));
        Assert.IsTrue(GameplayPresentationCueProjector.ShouldPresentRemoval(
            EntityKind.Player,
            EntityRemoveReason.Destroyed));
        Assert.IsTrue(GameplayPresentationCueProjector.ShouldPresentRemoval(
            EntityKind.Resource,
            EntityRemoveReason.Collected));
    }

    [TestMethod]
    public void RepeatedAuthoritativeObservationEmitsEachTransitionOnce()
    {
        GameplayPresentationCueProjector projector =
            new GameplayPresentationCueProjector();
        ClientWorldEntityKey controlledKey = new ClientWorldEntityKey(10, 3);

        IReadOnlyList<GameplayPresentationCue> spawn = projector.Project(
            Observation(1, controlledKey, 0, boostEnabled: false));
        IReadOnlyList<GameplayPresentationCue> repeatedSpawn = projector.Project(
            Observation(1, controlledKey, 0, boostEnabled: false));
        IReadOnlyList<GameplayPresentationCue> growthAndBoost = projector.Project(
            Observation(1, controlledKey, 1, boostEnabled: true));
        IReadOnlyList<GameplayPresentationCue> repeatedGrowthAndBoost = projector.Project(
            Observation(1, controlledKey, 1, boostEnabled: true));
        IReadOnlyList<GameplayPresentationCue> boostStop = projector.Project(
            Observation(1, controlledKey, 1, boostEnabled: false));

        CollectionAssert.AreEqual(
            new[] { GameplayPresentationCueKind.ControlledSpawn },
            Kinds(spawn));
        Assert.AreEqual(0, repeatedSpawn.Count);
        CollectionAssert.AreEqual(
            new[]
            {
                GameplayPresentationCueKind.GrowthChanged,
                GameplayPresentationCueKind.BoostStarted,
            },
            Kinds(growthAndBoost));
        Assert.AreEqual(0, repeatedGrowthAndBoost.Count);
        CollectionAssert.AreEqual(
            new[] { GameplayPresentationCueKind.BoostStopped },
            Kinds(boostStop));
    }

    [TestMethod]
    public void TransportGenerationResetsTransientCueIdentity()
    {
        GameplayPresentationCueProjector projector =
            new GameplayPresentationCueProjector();
        ClientWorldEntityKey controlledKey = new ClientWorldEntityKey(10, 3);
        GameplayPresentationObservation result = new GameplayPresentationObservation(
            1,
            controlledKey,
            3,
            false,
            7,
            900,
            RecipientWon: true);

        IReadOnlyList<GameplayPresentationCue> first = projector.Project(result);
        IReadOnlyList<GameplayPresentationCue> repeated = projector.Project(result);
        IReadOnlyList<GameplayPresentationCue> nextTransport = projector.Project(
            result with { TransportGeneration = 2 });

        CollectionAssert.Contains(
            Kinds(first),
            GameplayPresentationCueKind.ControlledSpawn);
        CollectionAssert.Contains(
            Kinds(first),
            GameplayPresentationCueKind.RoundResult);
        Assert.AreEqual(0, repeated.Count);
        CollectionAssert.Contains(
            Kinds(nextTransport),
            GameplayPresentationCueKind.ControlledSpawn);
        CollectionAssert.Contains(
            Kinds(nextTransport),
            GameplayPresentationCueKind.RoundResult);
    }

    [TestMethod]
    public void ControlledIdentityReplacementRequestsTransientPresentationReset()
    {
        GameplayPresentationCueProjector projector =
            new GameplayPresentationCueProjector();
        ClientWorldEntityKey firstKey = new ClientWorldEntityKey(10, 3);
        ClientWorldEntityKey replacementKey = new ClientWorldEntityKey(10, 4);

        _ = projector.Project(Observation(1, firstKey, 0, boostEnabled: false));
        _ = projector.Project(Observation(1, firstKey, 0, boostEnabled: true));
        IReadOnlyList<GameplayPresentationCue> replacement = projector.Project(
            Observation(1, replacementKey, 0, boostEnabled: false));

        CollectionAssert.AreEqual(
            new[]
            {
                GameplayPresentationCueKind.ControlledPresentationReset,
                GameplayPresentationCueKind.ControlledSpawn,
            },
            Kinds(replacement));
    }

    private static GameplayPresentationObservation Observation(
        uint transportGeneration,
        ClientWorldEntityKey controlledKey,
        uint growthPoint,
        bool boostEnabled)
    {
        return new GameplayPresentationObservation(
            transportGeneration,
            controlledKey,
            growthPoint,
            boostEnabled,
            null,
            null,
            RecipientWon: false);
    }

    private static GameplayPresentationCueKind[] Kinds(
        IReadOnlyList<GameplayPresentationCue> cues)
    {
        GameplayPresentationCueKind[] kinds =
            new GameplayPresentationCueKind[cues.Count];
        for (int index = 0; index < cues.Count; ++index)
        {
            kinds[index] = cues[index].Kind;
        }
        return kinds;
    }
}
