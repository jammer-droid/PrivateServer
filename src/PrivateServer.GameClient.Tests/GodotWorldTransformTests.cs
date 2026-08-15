using Microsoft.VisualStudio.TestTools.UnitTesting;

using Godot;
using System;

using PrivateServer.GameClient.Gameplay.Presentation;

using ControlledEntityBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityBodySample;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GodotWorldTransformTests
{
    [TestMethod]
    public void ConvertsServerPositionAndAngleToGodotSpace()
    {
        Godot.Vector2 position = GodotWorldTransform.Position(
            new System.Numerics.Vector2(2.0f, -3.0f),
            4.0f);

        Assert.AreEqual(8.0f, position.X, 0.0001f);
        Assert.AreEqual(12.0f, position.Y, 0.0001f);
        Assert.AreEqual(
            -MathF.PI / 2.0f,
            GodotWorldTransform.Angle(MathF.PI / 2.0f),
            0.0001f);
    }

    [TestMethod]
    public void ConvertsArenaToTopLeftGodotRectangle()
    {
        Rect2 arena = GodotWorldTransform.Arena(
            -10.0f,
            -5.0f,
            20.0f,
            15.0f,
            2.0f);

        Assert.AreEqual(-20.0f, arena.Position.X, 0.0001f);
        Assert.AreEqual(-30.0f, arena.Position.Y, 0.0001f);
        Assert.AreEqual(60.0f, arena.Size.X, 0.0001f);
        Assert.AreEqual(40.0f, arena.Size.Y, 0.0001f);
    }

    [TestMethod]
    public void BuildsControlledBodyFromRenderedHeadAndAuthoritativeSamples()
    {
        Vector2[] points = GodotWorldTransform.ControlledBodyTrail(
            new System.Numerics.Vector2(3.0f, 4.0f),
            new[]
            {
                new ControlledEntityBodySampleV2(2.0f, 4.0f),
                new ControlledEntityBodySampleV2(1.0f, 3.0f),
            },
            2.0f);

        CollectionAssert.AreEqual(
            new[]
            {
                new Vector2(6.0f, -8.0f),
                new Vector2(4.0f, -8.0f),
                new Vector2(2.0f, -6.0f),
            },
            points);
    }

    [TestMethod]
    public void BuildsRemoteBodyFromInterpolatedHeadAndTrail()
    {
        Vector2[] points = GodotWorldTransform.RemoteBodyTrail(
            new System.Numerics.Vector2(3.0f, 4.0f),
            new[]
            {
                new System.Numerics.Vector2(2.0f, 4.0f),
                new System.Numerics.Vector2(1.0f, 3.0f),
            },
            2.0f);

        CollectionAssert.AreEqual(
            new[]
            {
                new Vector2(6.0f, -8.0f),
                new Vector2(4.0f, -8.0f),
                new Vector2(2.0f, -6.0f),
            },
            points);
    }

    [TestMethod]
    public void FitsMinimapBoundsWithAspectRatioAndProjectsServerCoordinates()
    {
        MinimapProjection projection = GodotWorldTransform.Minimap(
            -10.0f,
            -5.0f,
            20.0f,
            15.0f,
            new Rect2(0.0f, 0.0f, 100.0f, 100.0f));

        Assert.AreEqual(0.0f, projection.Bounds.Position.X, 0.0001f);
        Assert.AreEqual(16.6667f, projection.Bounds.Position.Y, 0.0001f);
        Assert.AreEqual(100.0f, projection.Bounds.Size.X, 0.0001f);
        Assert.AreEqual(66.6667f, projection.Bounds.Size.Y, 0.0001f);
        Vector2 topLeft = projection.Position(-10.0f, 15.0f);
        Assert.AreEqual(0.0f, topLeft.X, 0.0001f);
        Assert.AreEqual(16.6667f, topLeft.Y, 0.0001f);
        Vector2 bottomRight = projection.Position(20.0f, -5.0f);
        Assert.AreEqual(100.0f, bottomRight.X, 0.0001f);
        Assert.AreEqual(83.3333f, bottomRight.Y, 0.0001f);
        Assert.AreEqual(13.3333f, projection.Radius(4.0f), 0.0001f);
    }

    [TestMethod]
    public void RejectsInvalidPresentationScale()
    {
        Assert.ThrowsException<ArgumentOutOfRangeException>(
            () => GodotWorldTransform.Position(
                System.Numerics.Vector2.Zero,
                0.0f));
    }
}
