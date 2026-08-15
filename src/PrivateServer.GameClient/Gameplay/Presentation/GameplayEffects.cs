using Godot;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class GameplayEffects : Node2D
{
    [Export]
    public PackedScene OneShotEffectScene { get; set; } = null!;

    internal void Play(
        GameplayOneShotEffectKind kind,
        Vector2 worldPosition,
        float radiusPixels)
    {
        GameplayOneShotEffect effect =
            OneShotEffectScene.Instantiate<GameplayOneShotEffect>();
        AddChild(effect);
        effect.Configure(kind, worldPosition, radiusPixels);
        effect.Play();
    }

    internal void ClearTransientEffects()
    {
        foreach (Node child in GetChildren())
        {
            child.QueueFree();
        }
    }
}
