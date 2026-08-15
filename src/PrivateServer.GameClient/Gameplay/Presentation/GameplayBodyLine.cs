using Godot;

using PrivateServer.GameClient.Gameplay.Replication;

using System;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class GameplayBodyLine : Node2D
{
    private const float OutlinePaddingPixels = 4.0f;
    private const float NormalGlowPaddingPixels = 10.0f;
    private const float BoostGlowPaddingPixels = 24.0f;

    private Line2D outlineLine = null!;
    private Line2D coreLine = null!;
    private Line2D glowLine = null!;
    private Color accentColor;
    private float diameterPixels;
    private bool boostActive;
    private bool sceneNodesBound;

    internal ClientWorldEntityKey Key { get; private set; }

    public override void _Ready()
    {
        BindSceneNodes();
    }

    internal void Configure(ClientWorldEntityKey key, bool isControlled)
    {
        BindSceneNodes();
        Key = key;
        Name = isControlled
            ? $"ControlledBody_{key.EntityId}_{key.Generation}"
            : $"RemoteBody_{key.EntityId}_{key.Generation}";
        ZIndex = isControlled ? 2 : 0;
        coreLine.DefaultColor = GameplayPalette.PlayerCore(key, isControlled);
        accentColor = GameplayPalette.PlayerAccent(key, isControlled);
        outlineLine.DefaultColor = GameplayPalette.Outline;
        boostActive = false;
        ApplyGlowColor();
        Clear();
    }

    internal void Apply(Vector2[] points, float widthPixels)
    {
        ArgumentNullException.ThrowIfNull(points);
        if (points.Length < 2 || !float.IsFinite(widthPixels) || widthPixels <= 0.0f)
        {
            Clear();
            return;
        }

        BindSceneNodes();
        diameterPixels = widthPixels;
        outlineLine.Points = points;
        glowLine.Points = points;
        coreLine.Points = points;
        ApplyLineWidths();
        Visible = true;
    }

    internal void ApplyBoost(bool isBoostActive)
    {
        if (boostActive == isBoostActive)
        {
            return;
        }

        boostActive = isBoostActive;
        BindSceneNodes();
        ApplyLineWidths();
        ApplyGlowColor();
    }

    internal void Clear()
    {
        BindSceneNodes();
        Vector2[] emptyPoints = Array.Empty<Vector2>();
        outlineLine.Points = emptyPoints;
        glowLine.Points = emptyPoints;
        coreLine.Points = emptyPoints;
        diameterPixels = 0.0f;
        Visible = false;
    }

    private void BindSceneNodes()
    {
        if (sceneNodesBound)
        {
            return;
        }

        outlineLine = GetNode<Line2D>("Outline");
        coreLine = GetNode<Line2D>("Core");
        glowLine = GetNode<Line2D>("Glow");
        sceneNodesBound = true;
    }

    private void ApplyLineWidths()
    {
        coreLine.Width = diameterPixels;
        outlineLine.Width = diameterPixels + OutlinePaddingPixels;
        float glowPaddingPixels = boostActive
            ? BoostGlowPaddingPixels
            : NormalGlowPaddingPixels;
        glowLine.Width = diameterPixels + glowPaddingPixels;
    }

    private void ApplyGlowColor()
    {
        float alpha = boostActive ? 0.9f : 0.34f;
        glowLine.DefaultColor = GameplayPalette.WithAlpha(accentColor, alpha);
    }
}
