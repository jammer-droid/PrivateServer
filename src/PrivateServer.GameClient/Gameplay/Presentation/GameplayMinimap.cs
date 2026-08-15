using Godot;
using PrivateServer.GameClient.Gameplay.Protocol.V2;
using PrivateServer.GameClient.Gameplay.Replication;
using System;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class GameplayMinimap : Control
{
    private static readonly Color ActiveAreaColor = new Color("f59e0b");
    private static readonly Color BackgroundColor = new Color(0.04f, 0.06f, 0.10f, 0.92f);
    private static readonly Color BorderColor = new Color("64748b");
    private static readonly Color RemotePlayerColor = new Color("38bdf8");
    private static readonly Color SelfPlayerColor = new Color("f8fafc");
    private const float ContentPadding = 8.0f;

    private WorldOverviewState? overview;
    private uint? selfPlayerId;

    internal void Apply(WorldOverviewState? nextOverview, uint? nextSelfPlayerId)
    {
        if (ReferenceEquals(overview, nextOverview) && selfPlayerId == nextSelfPlayerId)
        {
            return;
        }

        overview = nextOverview;
        selfPlayerId = nextSelfPlayerId;
        QueueRedraw();
    }

    public override void _Draw()
    {
        Rect2 panel = new Rect2(Vector2.Zero, Size);
        DrawRect(panel, BackgroundColor, true);
        DrawRect(panel, BorderColor, false, 2.0f, true);
        if (overview is null)
        {
            return;
        }

        Rect2 content = panel.Grow(-ContentPadding);
        MinimapProjection projection = GodotWorldTransform.Minimap(
            overview.MapMinX,
            overview.MapMinY,
            overview.MapMaxX,
            overview.MapMaxY,
            content);
        DrawRect(projection.Bounds, BorderColor, false, 1.0f, true);

        Vector2 activeAreaCenter = projection.Position(
            overview.ActiveAreaCenterX,
            overview.ActiveAreaCenterY);
        DrawArc(
            activeAreaCenter,
            projection.Radius(overview.ActiveAreaRadius),
            0.0f,
            MathF.Tau,
            64,
            ActiveAreaColor,
            2.0f,
            true);

        for (int playerIndex = 0; playerIndex < overview.Players.Count; ++playerIndex)
        {
            WorldOverviewPlayer player = overview.Players[playerIndex];
            Vector2[] bodyPoints = new Vector2[player.BodySamples.Count];
            for (int sampleIndex = 0; sampleIndex < player.BodySamples.Count; ++sampleIndex)
            {
                WorldOverviewPoint sample = player.BodySamples[sampleIndex];
                bodyPoints[sampleIndex] = projection.Position(sample.PositionX, sample.PositionY);
            }

            bool isSelf = selfPlayerId.HasValue && selfPlayerId.Value == player.PlayerId;
            Color color = isSelf ? SelfPlayerColor : RemotePlayerColor;
            if (bodyPoints.Length >= 2)
            {
                DrawPolyline(bodyPoints, color, isSelf ? 3.0f : 2.0f, true);
            }
            DrawCircle(bodyPoints[0], isSelf ? 3.5f : 2.5f, color, true, -1.0f, true);
        }
    }
}
