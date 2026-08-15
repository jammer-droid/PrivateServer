using System;
using System.Collections.Generic;

using PrivateServer.GameClient.Gameplay.Protocol.V1;

namespace PrivateServer.GameClient.Gameplay.Model;

internal enum AuthoritativeGameplayStateError
{
    None,
    StaleServerTick,
    StaleRoundId,
    MissingRoundState,
    InvalidTimeline,
}

internal readonly record struct LeaderboardEntry(
    uint Rank,
    uint PlayerId,
    uint Score,
    uint ServerTick,
    string DisplayName);

internal readonly record struct RoundPresentationState(
    RoundState AuthoritativeState,
    double RemainingTicks,
    double RemainingSeconds);

internal sealed class AuthoritativeGameplayState
{
    private readonly Dictionary<uint, ScoreState> scores = new();

    internal int ScoreCount => scores.Count;
    internal RoundState? Round { get; private set; }

    internal AuthoritativeGameplayStateError ApplyScore(ScoreState score)
    {
        ArgumentNullException.ThrowIfNull(score);
        if (scores.TryGetValue(score.PlayerId, out ScoreState? existing) &&
            score.ServerTick < existing.ServerTick)
        {
            return AuthoritativeGameplayStateError.StaleServerTick;
        }

        scores[score.PlayerId] = score;
        return AuthoritativeGameplayStateError.None;
    }

    internal AuthoritativeGameplayStateError ApplyRound(RoundState round)
    {
        ArgumentNullException.ThrowIfNull(round);
        if (Round is not null)
        {
            if (round.ServerTick < Round.ServerTick)
            {
                return AuthoritativeGameplayStateError.StaleServerTick;
            }
            if (round.RoundId < Round.RoundId)
            {
                return AuthoritativeGameplayStateError.StaleRoundId;
            }
        }

        Round = round;
        return AuthoritativeGameplayStateError.None;
    }

    internal IReadOnlyList<LeaderboardEntry> BuildLeaderboard()
    {
        List<LeaderboardEntry> entries = new List<LeaderboardEntry>(scores.Count);
        foreach (ScoreState score in scores.Values)
        {
            entries.Add(new LeaderboardEntry(0, score.PlayerId, score.Score, score.ServerTick, string.Empty));
        }

        entries.Sort(static (left, right) =>
        {
            int scoreOrder = right.Score.CompareTo(left.Score);
            return scoreOrder != 0
                ? scoreOrder
                : left.PlayerId.CompareTo(right.PlayerId);
        });
        for (int index = 0; index < entries.Count; ++index)
        {
            LeaderboardEntry entry = entries[index];
            entries[index] = entry with { Rank = checked((uint)index + 1) };
        }
        return entries.AsReadOnly();
    }

    internal AuthoritativeGameplayStateError ProjectRound(
        double estimatedServerTimeline,
        uint tickRateHz,
        out RoundPresentationState presentation)
    {
        presentation = default;
        if (Round is null)
        {
            return AuthoritativeGameplayStateError.MissingRoundState;
        }
        if (!double.IsFinite(estimatedServerTimeline) ||
            estimatedServerTimeline < 0.0 ||
            tickRateHz == 0)
        {
            return AuthoritativeGameplayStateError.InvalidTimeline;
        }

        double remainingTicks = Math.Max(
            0.0,
            Round.PhaseEndsAtServerTick - estimatedServerTimeline);
        presentation = new RoundPresentationState(
            Round,
            remainingTicks,
            remainingTicks / tickRateHz);
        return AuthoritativeGameplayStateError.None;
    }

    internal void Clear()
    {
        scores.Clear();
        Round = null;
    }
}
