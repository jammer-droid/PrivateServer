using Godot;
using System;
using System.Collections.Generic;

using ControlledEntityBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityBodySample;

using NumericsVector2 = System.Numerics.Vector2;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal readonly record struct MinimapProjection(
    Vector2 Origin,
    float Scale,
    float WorldMinX,
    float WorldMaxY,
    Vector2 WorldSize)
{
    internal Vector2 Position(float positionX, float positionY)
    {
        return Origin + new Vector2(
            (positionX - WorldMinX) * Scale,
            (WorldMaxY - positionY) * Scale);
    }

    internal float Radius(float worldRadius)
    {
        return worldRadius * Scale;
    }

    internal Rect2 Bounds => new Rect2(Origin, WorldSize * Scale);
}

internal static class GodotWorldTransform
{
    internal static Vector2 Position(NumericsVector2 serverPosition, float pixelsPerUnit)
    {
        ValidatePixelsPerUnit(pixelsPerUnit);
        return new Vector2(
            serverPosition.X * pixelsPerUnit,
            -serverPosition.Y * pixelsPerUnit);
    }

    internal static float Angle(float serverAngleRadians)
    {
        if (!float.IsFinite(serverAngleRadians))
        {
            throw new ArgumentOutOfRangeException(nameof(serverAngleRadians));
        }

        return -serverAngleRadians;
    }

    internal static Vector2[] ControlledBodyTrail(
        NumericsVector2 renderedHead,
        IReadOnlyList<ControlledEntityBodySampleV2> authoritativeSamples,
        float pixelsPerUnit)
    {
        ArgumentNullException.ThrowIfNull(authoritativeSamples);
        ValidatePixelsPerUnit(pixelsPerUnit);

        Vector2[] points = new Vector2[authoritativeSamples.Count + 1];
        points[0] = new Vector2(
            renderedHead.X * pixelsPerUnit,
            -renderedHead.Y * pixelsPerUnit);
        for (int index = 0; index < authoritativeSamples.Count; ++index)
        {
            ControlledEntityBodySampleV2 sample = authoritativeSamples[index];
            points[index + 1] = new Vector2(
                sample.PositionX * pixelsPerUnit,
                -sample.PositionY * pixelsPerUnit);
        }
        return points;
    }

    internal static Vector2[] RemoteBodyTrail(
        NumericsVector2 renderedHead,
        IReadOnlyList<NumericsVector2> bodyTrail,
        float pixelsPerUnit)
    {
        ArgumentNullException.ThrowIfNull(bodyTrail);
        ValidatePixelsPerUnit(pixelsPerUnit);

        Vector2[] points = new Vector2[bodyTrail.Count + 1];
        points[0] = Position(renderedHead, pixelsPerUnit);
        for (int index = 0; index < bodyTrail.Count; ++index)
        {
            points[index + 1] = Position(bodyTrail[index], pixelsPerUnit);
        }
        return points;
    }

    internal static Rect2 Arena(
        float minX,
        float minY,
        float maxX,
        float maxY,
        float pixelsPerUnit)
    {
        ValidatePixelsPerUnit(pixelsPerUnit);
        if (!float.IsFinite(minX) ||
            !float.IsFinite(minY) ||
            !float.IsFinite(maxX) ||
            !float.IsFinite(maxY) ||
            minX >= maxX ||
            minY >= maxY)
        {
            throw new ArgumentOutOfRangeException(nameof(minX));
        }

        Vector2 topLeft = Position(
            new NumericsVector2(minX, maxY),
            pixelsPerUnit);
        Vector2 size = new Vector2(
            (maxX - minX) * pixelsPerUnit,
            (maxY - minY) * pixelsPerUnit);
        return new Rect2(topLeft, size);
    }

    internal static MinimapProjection Minimap(
        float minX,
        float minY,
        float maxX,
        float maxY,
        Rect2 frame)
    {
        if (!float.IsFinite(minX) ||
            !float.IsFinite(minY) ||
            !float.IsFinite(maxX) ||
            !float.IsFinite(maxY) ||
            minX >= maxX ||
            minY >= maxY ||
            frame.Size.X <= 0.0f ||
            frame.Size.Y <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(minX));
        }

        Vector2 worldSize = new Vector2(maxX - minX, maxY - minY);
        float scale = MathF.Min(
            frame.Size.X / worldSize.X,
            frame.Size.Y / worldSize.Y);
        Vector2 renderedSize = worldSize * scale;
        Vector2 origin = frame.Position + (frame.Size - renderedSize) * 0.5f;
        return new MinimapProjection(origin, scale, minX, maxY, worldSize);
    }

    private static void ValidatePixelsPerUnit(float pixelsPerUnit)
    {
        if (!float.IsFinite(pixelsPerUnit) || pixelsPerUnit <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(pixelsPerUnit));
        }
    }
}
