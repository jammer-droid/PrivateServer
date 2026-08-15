using System.Collections.Generic;

namespace PrivateServer.GameClient.Gameplay.Flow;

internal readonly record struct GameplayLaunchOptions(uint? ObserverChannelId)
{
    private const string ObserveChannelArgument = "--observe-channel";

    internal static bool TryParse(
        IReadOnlyList<string> arguments,
        out GameplayLaunchOptions options)
    {
        options = default;
        uint? observerChannelId = null;
        for (int index = 0; index < arguments.Count; ++index)
        {
            if (arguments[index] != ObserveChannelArgument ||
                observerChannelId.HasValue ||
                index + 1 >= arguments.Count ||
                !uint.TryParse(arguments[index + 1], out uint parsedChannelId) ||
                parsedChannelId == 0)
            {
                return false;
            }

            observerChannelId = parsedChannelId;
            ++index;
        }

        options = new GameplayLaunchOptions(observerChannelId);
        return true;
    }
}
