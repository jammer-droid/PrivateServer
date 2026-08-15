using Godot;

using PrivateServer.GameClient.Gameplay.Replication;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal static class GameplayPalette
{
    internal static readonly Color ControlledCore = Colors.White;
    internal static readonly Color ControlledAccent = new Color("22d3ee");
    internal static readonly Color ResourceCore = new Color("facc15");
    internal static readonly Color ResourceAccent = new Color("fde68a");
    internal static readonly Color StaticObstacleCore = new Color("8b93a7");
    internal static readonly Color StaticObstacleAccent = new Color("cbd5e1");
    internal static readonly Color Outline = new Color("151923");

    private static readonly Color[] RemotePlayerColors =
    {
        new Color("fb7185"),
        new Color("f97316"),
        new Color("a78bfa"),
        new Color("4ade80"),
        new Color("60a5fa"),
        new Color("f472b6"),
    };

    internal static Color PlayerCore(
        ClientWorldEntityKey key,
        bool isControlled)
    {
        if (isControlled)
        {
            return ControlledCore;
        }

        uint paletteIndex = StablePaletteIndex(key.EntityId);
        int colorIndex = (int)(paletteIndex % (uint)RemotePlayerColors.Length);
        return RemotePlayerColors[colorIndex];
    }

    internal static Color PlayerAccent(
        ClientWorldEntityKey key,
        bool isControlled)
    {
        if (isControlled)
        {
            return ControlledAccent;
        }

        return PlayerCore(key, isControlled: false).Lightened(0.22f);
    }

    internal static Color WithAlpha(Color color, float alpha)
    {
        return new Color(color.R, color.G, color.B, alpha);
    }

    private static uint StablePaletteIndex(uint entityId)
    {
        unchecked
        {
            uint value = entityId;
            value ^= value >> 16;
            value *= 0x7feb352dU;
            value ^= value >> 15;
            value *= 0x846ca68bU;
            value ^= value >> 16;
            return value;
        }
    }
}
