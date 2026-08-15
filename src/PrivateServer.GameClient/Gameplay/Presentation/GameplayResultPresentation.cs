using PrivateServer.GameClient.Gameplay.Model;
using PrivateServer.GameClient.Gameplay.Protocol.V2;
using System;
using System.Collections.Generic;
using System.Text;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal readonly record struct GameplayResultPresentation(
    string Title,
    string Details)
{
    private const int MaximumDisplayedWinnerIds = 8;

    internal static GameplayResultPresentation From(
        RoundResult result,
        uint? recipientPlayerId,
        IReadOnlyList<LeaderboardEntry>? finalLeaderboard = null)
    {
        ArgumentNullException.ThrowIfNull(result);

        bool hasWinner = result.WinnerPlayerIds.Count > 0;
        bool recipientWon = IsRecipientWinner(result, recipientPlayerId);

        StringBuilder details = new StringBuilder();
        details.Append($"Round {result.RoundId}\n");
        if (recipientPlayerId.HasValue)
        {
            details.Append($"Final growth: {result.RecipientFinalGrowthPoint}\n");
        }
        if (!hasWinner)
        {
            details.Append("Winner: none");
        }
        else
        {
            details.Append($"Winning growth: {result.WinningGrowthPoint}\n");
            details.Append(result.WinnerPlayerIds.Count == 1
                ? "Winner: "
                : "Co-winners: ");
            int displayedWinnerCount = Math.Min(
                result.WinnerPlayerIds.Count,
                MaximumDisplayedWinnerIds);
            for (int index = 0; index < displayedWinnerCount; ++index)
            {
                if (index > 0)
                {
                    details.Append(", ");
                }
                uint winnerPlayerId = result.WinnerPlayerIds[index];
                details.Append(ResolveWinnerLabel(finalLeaderboard, winnerPlayerId));
            }
            int hiddenWinnerCount =
                result.WinnerPlayerIds.Count - displayedWinnerCount;
            if (hiddenWinnerCount > 0)
            {
                details.Append($", +{hiddenWinnerCount} more");
            }
        }

        string title;
        if (!hasWinner)
        {
            title = "NO WINNER";
        }
        else if (recipientWon)
        {
            title = result.WinnerPlayerIds.Count > 1
                ? "CO-VICTORY"
                : "VICTORY";
        }
        else
        {
            title = result.WinnerPlayerIds.Count > 1
                ? "CO-WINNERS"
                : "ROUND COMPLETE";
        }

        return new GameplayResultPresentation(title, details.ToString());
    }

    private static string ResolveWinnerLabel(
        IReadOnlyList<LeaderboardEntry>? finalLeaderboard,
        uint playerId)
    {
        if (finalLeaderboard is not null)
        {
            for (int index = 0; index < finalLeaderboard.Count; ++index)
            {
                LeaderboardEntry entry = finalLeaderboard[index];
                if (entry.PlayerId == playerId && !string.IsNullOrEmpty(entry.DisplayName))
                {
                    return entry.DisplayName;
                }
            }
        }
        return $"player {playerId}";
    }

    internal static bool IsRecipientWinner(
        RoundResult result,
        uint? recipientPlayerId)
    {
        ArgumentNullException.ThrowIfNull(result);
        if (!recipientPlayerId.HasValue)
        {
            return false;
        }

        for (int index = 0; index < result.WinnerPlayerIds.Count; ++index)
        {
            if (result.WinnerPlayerIds[index] == recipientPlayerId.Value)
            {
                return true;
            }
        }
        return false;
    }
}
