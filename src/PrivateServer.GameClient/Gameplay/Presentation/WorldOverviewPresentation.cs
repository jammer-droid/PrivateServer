using Godot;
using PrivateServer.GameClient.Gameplay.Protocol.V2;
using PrivateServer.GameClient.Gameplay.Replication;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class WorldOverviewPresentation : Node2D
{
    private const float BodyWidthWorldUnits = 1.0f;
    private WorldOverviewState? overview;
    private float pixelsPerUnit;

    internal void Apply(
        WorldOverviewState? nextOverview,
        float nextPixelsPerUnit,
        bool visible)
    {
        Visible = visible;
        if (ReferenceEquals(overview, nextOverview) &&
            pixelsPerUnit == nextPixelsPerUnit)
        {
            return;
        }

        overview = nextOverview;
        pixelsPerUnit = nextPixelsPerUnit;
        QueueRedraw();
    }

    public override void _Draw()
    {
        if (overview is null || pixelsPerUnit <= 0.0f)
        {
            return;
        }

        float bodyWidthPixels = BodyWidthWorldUnits * pixelsPerUnit;
        for (int playerIndex = 0; playerIndex < overview.Players.Count; ++playerIndex)
        {
            WorldOverviewPlayer player = overview.Players[playerIndex];
            Vector2[] points = new Vector2[player.BodySamples.Count];
            for (int sampleIndex = 0; sampleIndex < player.BodySamples.Count; ++sampleIndex)
            {
                WorldOverviewPoint sample = player.BodySamples[sampleIndex];
                points[sampleIndex] = GodotWorldTransform.Position(
                    new System.Numerics.Vector2(sample.PositionX, sample.PositionY),
                    pixelsPerUnit);
            }

            ClientWorldEntityKey paletteKey = new ClientWorldEntityKey(player.PlayerId, 1);
            Color core = GameplayPalette.PlayerCore(paletteKey, isControlled: false);
            Color accent = GameplayPalette.PlayerAccent(paletteKey, isControlled: false);
            if (points.Length >= 2)
            {
                DrawPolyline(
                    points,
                    GameplayPalette.WithAlpha(accent, 0.32f),
                    bodyWidthPixels + 10.0f,
                    true);
                DrawPolyline(points, core, bodyWidthPixels, true);
            }
            DrawCircle(
                points[0],
                bodyWidthPixels * 0.5f,
                core,
                true,
                -1.0f,
                true);
        }
    }
}
