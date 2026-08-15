using Godot;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Replication;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class ArenaPresentation : Node2D
{
    private const int ActiveAreaBoundarySegmentCount = 128;

    private Sprite2D gridPattern = null!;
    private Line2D mapBoundaryGlow = null!;
    private Line2D mapBoundary = null!;
    private Line2D activeAreaBoundary = null!;
    private Rect2? arenaRect;
    private Vector2? activeAreaCenter;
    private float activeAreaRadiusPixels;

    internal Rect2? ArenaRect => arenaRect;

    public override void _Ready()
    {
        gridPattern = GetNode<Sprite2D>("%GridPattern");
        mapBoundaryGlow = GetNode<Line2D>("%MapBoundaryGlow");
        mapBoundary = GetNode<Line2D>("%MapBoundary");
        activeAreaBoundary = GetNode<Line2D>("%ActiveAreaBoundary");
    }

    internal void ApplyArena(WorldReady? ready, float pixelsPerUnit)
    {
        if (ready is null)
        {
            ClearArena();
            return;
        }

        ApplyArenaBounds(
            ready.ArenaMinX,
            ready.ArenaMinY,
            ready.ArenaMaxX,
            ready.ArenaMaxY,
            pixelsPerUnit);
    }

    internal void ApplyArena(ObserverReady? ready, float pixelsPerUnit)
    {
        if (ready is null)
        {
            ClearArena();
            return;
        }

        ApplyArenaBounds(
            ready.ArenaMinX,
            ready.ArenaMinY,
            ready.ArenaMaxX,
            ready.ArenaMaxY,
            pixelsPerUnit);
    }

    private void ApplyArenaBounds(
        float arenaMinX,
        float arenaMinY,
        float arenaMaxX,
        float arenaMaxY,
        float pixelsPerUnit)
    {
        Rect2 nextArena = GodotWorldTransform.Arena(
            arenaMinX,
            arenaMinY,
            arenaMaxX,
            arenaMaxY,
            pixelsPerUnit);
        if (arenaRect.HasValue && arenaRect.Value == nextArena)
        {
            return;
        }

        arenaRect = nextArena;
        gridPattern.Position = nextArena.Position;
        gridPattern.RegionRect = new Rect2(Vector2.Zero, nextArena.Size);
        gridPattern.Visible = true;

        Vector2 topLeft = nextArena.Position;
        Vector2 topRight = new Vector2(nextArena.End.X, nextArena.Position.Y);
        Vector2 bottomRight = nextArena.End;
        Vector2 bottomLeft = new Vector2(nextArena.Position.X, nextArena.End.Y);
        Vector2[] boundaryPoints =
        {
            topLeft,
            topRight,
            bottomRight,
            bottomLeft,
            topLeft,
        };
        mapBoundaryGlow.Points = boundaryPoints;
        mapBoundary.Points = boundaryPoints;
        mapBoundaryGlow.Visible = true;
        mapBoundary.Visible = true;
    }

    private void ClearArena()
    {
        arenaRect = null;
        gridPattern.Visible = false;
        mapBoundaryGlow.Visible = false;
        mapBoundary.Visible = false;
    }

    internal void ApplyActiveArea(WorldOverviewState? overview, float pixelsPerUnit)
    {
        if (overview is null)
        {
            activeAreaCenter = null;
            activeAreaRadiusPixels = 0.0f;
            activeAreaBoundary.Visible = false;
            return;
        }

        Vector2 center = GodotWorldTransform.Position(
            new System.Numerics.Vector2(
                overview.ActiveAreaCenterX,
                overview.ActiveAreaCenterY),
            pixelsPerUnit);
        float radiusPixels = Mathf.Max(1.0f, overview.ActiveAreaRadius * pixelsPerUnit);
        activeAreaCenter = center;
        activeAreaRadiusPixels = radiusPixels;
        activeAreaBoundary.Position = center;
        activeAreaBoundary.Points = BuildCirclePoints(radiusPixels);
        activeAreaBoundary.Visible = true;
    }

    internal bool IsNearActiveAreaBoundary(
        Vector2 renderedPosition,
        float warningBandPixels)
    {
        if (!activeAreaCenter.HasValue ||
            !float.IsFinite(warningBandPixels) ||
            warningBandPixels <= 0.0f)
        {
            return false;
        }

        float distance = renderedPosition.DistanceTo(activeAreaCenter.Value);
        return distance >= activeAreaRadiusPixels - warningBandPixels;
    }

    private static Vector2[] BuildCirclePoints(float radiusPixels)
    {
        Vector2[] points = new Vector2[ActiveAreaBoundarySegmentCount + 1];
        for (int index = 0; index <= ActiveAreaBoundarySegmentCount; ++index)
        {
            float angle = Mathf.Tau * index / ActiveAreaBoundarySegmentCount;
            points[index] = new Vector2(
                Mathf.Cos(angle) * radiusPixels,
                Mathf.Sin(angle) * radiusPixels);
        }
        return points;
    }
}
