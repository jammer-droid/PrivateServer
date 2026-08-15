using Godot;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal enum GameplayOneShotEffectKind
{
    Spawn,
    Collected,
    Destroyed,
    Growth,
}

internal partial class GameplayOneShotEffect : Node2D
{
    private Node2D visualRoot = null!;
    private Sprite2D ring = null!;
    private Sprite2D burst = null!;
    private Sprite2D spark = null!;
    private Sprite2D smoke = null!;
    private AnimationPlayer animation = null!;

    public override void _Ready()
    {
        visualRoot = GetNode<Node2D>("%VisualRoot");
        ring = GetNode<Sprite2D>("%Ring");
        burst = GetNode<Sprite2D>("%Burst");
        spark = GetNode<Sprite2D>("%Spark");
        smoke = GetNode<Sprite2D>("%Smoke");
        animation = GetNode<AnimationPlayer>("%Animation");
        animation.AnimationFinished += OnAnimationFinished;
    }

    internal void Configure(
        GameplayOneShotEffectKind kind,
        Vector2 worldPosition,
        float radiusPixels)
    {
        Position = worldPosition;
        float effectScale = Mathf.Clamp(radiusPixels / 32.0f, 0.45f, 2.4f);
        Scale = Vector2.One * effectScale;

        ring.Visible = kind != GameplayOneShotEffectKind.Destroyed;
        burst.Visible = true;
        spark.Visible = kind == GameplayOneShotEffectKind.Destroyed;
        smoke.Visible = kind == GameplayOneShotEffectKind.Destroyed;
        Color color = kind switch
        {
            GameplayOneShotEffectKind.Spawn => new Color("67e8f9"),
            GameplayOneShotEffectKind.Collected => new Color("facc15"),
            GameplayOneShotEffectKind.Destroyed => new Color("fb7185"),
            GameplayOneShotEffectKind.Growth => new Color("a7f3d0"),
            _ => Colors.White,
        };
        ring.Modulate = color;
        burst.Modulate = color;
        spark.Modulate = color;
        smoke.Modulate = new Color(color.R, color.G, color.B, 0.72f);
    }

    internal void Play()
    {
        visualRoot.Visible = true;
        animation.Play("burst");
    }

    private void OnAnimationFinished(StringName animationName)
    {
        if (animationName == "burst")
        {
            QueueFree();
        }
    }
}
