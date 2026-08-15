using Godot;

using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Replication;

namespace PrivateServer.GameClient.Gameplay.Presentation;

public partial class GameplayReplicaNode : Node2D
{
    private const float MinimumDisplayRadiusPixels = 5.0f;
    private const float GlowDiameterMultiplier = 1.55f;
    private const float HaloDiameterMultiplier = 1.45f;
    private const float BoostGlowDiameterMultiplier = 2.2f;
    private const float BoostHaloDiameterMultiplier = 1.9f;
    private const float ResourceGlowDiameterMultiplier = 1.7f;
    private const float ResourceAccentDiameterMultiplier = 0.72f;

    private Sprite2D coreSprite = null!;
    private Sprite2D outlineSprite = null!;
    private Sprite2D glowSprite = null!;
    private Sprite2D haloSprite = null!;
    private Sprite2D resourceGlowSprite = null!;
    private Sprite2D resourceAccentSprite = null!;
    private Line2D headingLine = null!;
    private Label playerLabel = null!;
    private AnimationPlayer animationPlayer = null!;
    private float displayRadiusPixels;
    private bool controlled;
    private bool boostActive;
    private bool sceneNodesBound;
    private bool configured;
    private EntityKind entityKind;

    internal ClientWorldEntityKey Key { get; private set; }
    internal float DisplayRadiusPixels => displayRadiusPixels;
    internal EntityKind EntityKind => entityKind;

    public override void _Ready()
    {
        BindSceneNodes();
        if (!configured)
        {
            return;
        }

        ApplyVisualScale();
        ApplyVisualAppearance();
    }

    internal void Configure(
        ClientWorldEntityKey key,
        EntityKind entityKind,
        float worldRadius,
        float pixelsPerUnit,
        bool isControlled)
    {
        BindSceneNodes();
        Key = key;
        controlled = isControlled;
        this.entityKind = entityKind;
        boostActive = false;
        configured = true;
        Name = isControlled
            ? $"Controlled_{key.EntityId}_{key.Generation}"
            : $"{entityKind}_{key.EntityId}_{key.Generation}";
        ZIndex = isControlled ? 3 : 1;
        ApplyRadius(worldRadius, pixelsPerUnit);
        ApplyVisualAppearance();
    }

    internal void ApplyRadius(float worldRadius, float pixelsPerUnit)
    {
        float nextRadiusPixels = Mathf.Max(
            MinimumDisplayRadiusPixels,
            worldRadius * pixelsPerUnit);
        if (displayRadiusPixels == nextRadiusPixels)
        {
            return;
        }

        displayRadiusPixels = nextRadiusPixels;
        BindSceneNodes();
        ApplyVisualScale();
    }

    internal void ApplyTransform(
        System.Numerics.Vector2 serverPosition,
        float serverAngleRadians,
        float pixelsPerUnit)
    {
        Position = GodotWorldTransform.Position(serverPosition, pixelsPerUnit);
        Rotation = GodotWorldTransform.Angle(serverAngleRadians);
        ApplyPlayerLabelTransform();
    }

    internal void ApplyPlayerLabel(string label)
    {
        BindSceneNodes();
        playerLabel.Text = label;
        playerLabel.Visible = entityKind == EntityKind.Player && !string.IsNullOrEmpty(label);
    }

    internal void ApplyBoost(bool isBoostActive)
    {
        if (boostActive == isBoostActive)
        {
            return;
        }

        boostActive = isBoostActive;
        BindSceneNodes();
        ApplyVisualScale();
        ApplyVisualAppearance();
    }

    private void BindSceneNodes()
    {
        if (sceneNodesBound)
        {
            return;
        }

        coreSprite = GetNode<Sprite2D>("Core");
        outlineSprite = GetNode<Sprite2D>("Outline");
        glowSprite = GetNode<Sprite2D>("Glow");
        haloSprite = GetNode<Sprite2D>("Halo");
        resourceGlowSprite = GetNode<Sprite2D>("ResourceGlow");
        resourceAccentSprite = GetNode<Sprite2D>("ResourceAccent");
        headingLine = GetNode<Line2D>("Heading");
        playerLabel = GetNode<Label>("PlayerLabel");
        animationPlayer = GetNode<AnimationPlayer>("AnimationPlayer");
        sceneNodesBound = true;
    }

    private void ApplyVisualScale()
    {
        float diameterPixels = displayRadiusPixels * 2.0f;
        ApplySpriteDiameter(coreSprite, diameterPixels);
        ApplySpriteDiameter(outlineSprite, diameterPixels);
        ApplySpriteDiameter(
            glowSprite,
            diameterPixels * (boostActive
                ? BoostGlowDiameterMultiplier
                : GlowDiameterMultiplier));
        ApplySpriteDiameter(
            haloSprite,
            diameterPixels * (boostActive
                ? BoostHaloDiameterMultiplier
                : HaloDiameterMultiplier));
        ApplySpriteDiameter(
            resourceGlowSprite,
            diameterPixels * ResourceGlowDiameterMultiplier);
        ApplySpriteDiameter(
            resourceAccentSprite,
            diameterPixels * ResourceAccentDiameterMultiplier);
        headingLine.Points = new Vector2[]
        {
            Vector2.Zero,
            new Vector2(
                displayRadiusPixels * (boostActive ? 1.8f : 1.0f),
                0.0f),
        };
        ApplyPlayerLabelTransform();
    }

    private void ApplyVisualAppearance()
    {
        bool isPlayer = entityKind == EntityKind.Player;
        bool isResource = entityKind == EntityKind.Resource;
        Color coreColor;
        Color accentColor;

        if (isPlayer)
        {
            coreColor = GameplayPalette.PlayerCore(Key, controlled);
            accentColor = GameplayPalette.PlayerAccent(Key, controlled);
        }
        else if (isResource)
        {
            coreColor = GameplayPalette.ResourceCore;
            accentColor = GameplayPalette.ResourceAccent;
        }
        else if (entityKind == EntityKind.StaticObstacle)
        {
            coreColor = GameplayPalette.StaticObstacleCore;
            accentColor = GameplayPalette.StaticObstacleAccent;
        }
        else
        {
            coreColor = Colors.Magenta;
            accentColor = Colors.Magenta;
        }

        coreSprite.Modulate = coreColor;
        outlineSprite.Modulate = GameplayPalette.Outline;
        glowSprite.Visible = isPlayer;
        glowSprite.Modulate = GameplayPalette.WithAlpha(
            accentColor,
            boostActive ? 0.78f : controlled ? 0.52f : 0.38f);
        haloSprite.Visible = isPlayer && controlled;
        haloSprite.Modulate = GameplayPalette.WithAlpha(
            GameplayPalette.ControlledAccent,
            boostActive ? 0.95f : 0.72f);
        resourceGlowSprite.Visible = isResource;
        resourceGlowSprite.Modulate = GameplayPalette.WithAlpha(
            GameplayPalette.ResourceCore,
            0.62f);
        resourceAccentSprite.Visible = isResource;
        resourceAccentSprite.Modulate = GameplayPalette.ResourceAccent;
        headingLine.Visible = !isResource;
        headingLine.DefaultColor = accentColor;

        if (isResource)
        {
            animationPlayer.Play("resource_idle");
        }
        else
        {
            animationPlayer.Stop();
        }
    }

    private void ApplyPlayerLabelTransform()
    {
        Vector2 pivot = new Vector2(80.0f, 12.0f);
        Vector2 screenOffset = new Vector2(0.0f, -displayRadiusPixels - 18.0f);
        playerLabel.PivotOffset = pivot;
        playerLabel.Position = screenOffset.Rotated(-Rotation) - pivot;
        playerLabel.Rotation = -Rotation;
    }

    private static void ApplySpriteDiameter(
        Sprite2D sprite,
        float diameterPixels)
    {
        Texture2D texture = sprite.Texture;
        int textureWidth = texture.GetWidth();
        if (textureWidth <= 0)
        {
            sprite.Scale = Vector2.One;
            return;
        }

        float uniformScale = diameterPixels / textureWidth;
        sprite.Scale = new Vector2(uniformScale, uniformScale);
    }
}
