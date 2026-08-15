using System.Collections.Generic;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Model;
using PrivateServer.GameClient.Gameplay.Protocol.V1;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class AuthoritativeGameplayStateTests
{
    [TestMethod]
    public void LeaderboardUsesAuthoritativeScoresWithStableOrdering()
    {
        AuthoritativeGameplayState state = new AuthoritativeGameplayState();
        state.ApplyScore(new ScoreState(10, 30, 5));
        state.ApplyScore(new ScoreState(10, 10, 7));
        state.ApplyScore(new ScoreState(10, 20, 7));

        IReadOnlyList<LeaderboardEntry> leaderboard = state.BuildLeaderboard();

        Assert.AreEqual(3, leaderboard.Count);
        Assert.AreEqual(1u, leaderboard[0].Rank);
        Assert.AreEqual(10u, leaderboard[0].PlayerId);
        Assert.AreEqual(2u, leaderboard[1].Rank);
        Assert.AreEqual(20u, leaderboard[1].PlayerId);
        Assert.AreEqual(3u, leaderboard[2].Rank);
        Assert.AreEqual(30u, leaderboard[2].PlayerId);
        Assert.AreEqual(7u, leaderboard[0].Score);
        Assert.AreEqual(7u, leaderboard[1].Score);
        Assert.AreEqual(5u, leaderboard[2].Score);

        Assert.AreEqual(
            AuthoritativeGameplayStateError.None,
            state.ApplyScore(new ScoreState(11, 30, 9)));
        Assert.AreEqual(30u, state.BuildLeaderboard()[0].PlayerId);
    }

    [TestMethod]
    public void RejectsStaleScoreAndRoundWithoutMutatingCurrentState()
    {
        AuthoritativeGameplayState state = new AuthoritativeGameplayState();
        state.ApplyScore(new ScoreState(20, 10, 5));
        state.ApplyRound(new RoundState(
            20,
            2,
            RoundPhase.Running,
            100,
            10,
            0));

        Assert.AreEqual(
            AuthoritativeGameplayStateError.StaleServerTick,
            state.ApplyScore(new ScoreState(19, 10, 99)));
        Assert.AreEqual(5u, state.BuildLeaderboard()[0].Score);
        Assert.AreEqual(
            AuthoritativeGameplayStateError.StaleServerTick,
            state.ApplyRound(new RoundState(
                19,
                3,
                RoundPhase.Ended,
                120,
                10,
                10)));
        Assert.AreEqual(
            AuthoritativeGameplayStateError.StaleRoundId,
            state.ApplyRound(new RoundState(
                21,
                1,
                RoundPhase.Running,
                120,
                10,
                0)));
        Assert.IsNotNull(state.Round);
        Assert.AreEqual(2u, state.Round.RoundId);
        Assert.AreEqual(RoundPhase.Running, state.Round.Phase);
    }

    [TestMethod]
    public void RoundProjectionOnlyComputesCountdownAndDoesNotInferTransition()
    {
        AuthoritativeGameplayState state = new AuthoritativeGameplayState();
        RoundState running = new RoundState(
            20,
            2,
            RoundPhase.Running,
            100,
            10,
            0);
        state.ApplyRound(running);

        Assert.AreEqual(
            AuthoritativeGameplayStateError.None,
            state.ProjectRound(
                85.5,
                10,
                out RoundPresentationState activePresentation));
        Assert.AreSame(running, activePresentation.AuthoritativeState);
        Assert.AreEqual(14.5, activePresentation.RemainingTicks, 0.0001);
        Assert.AreEqual(1.45, activePresentation.RemainingSeconds, 0.0001);

        state.ProjectRound(
            120,
            10,
            out RoundPresentationState expiredPresentation);
        Assert.AreEqual(0.0, expiredPresentation.RemainingTicks);
        Assert.AreEqual(
            RoundPhase.Running,
            expiredPresentation.AuthoritativeState.Phase,
            "Countdown expiry must not create a client-side round transition.");
    }

    [TestMethod]
    public void ClearRemovesGenerationLocalScoreAndRoundState()
    {
        AuthoritativeGameplayState state = new AuthoritativeGameplayState();
        state.ApplyScore(new ScoreState(10, 10, 5));
        state.ApplyRound(new RoundState(
            10,
            1,
            RoundPhase.Running,
            100,
            10,
            0));

        state.Clear();

        Assert.AreEqual(0, state.ScoreCount);
        Assert.IsNull(state.Round);
        Assert.AreEqual(
            AuthoritativeGameplayStateError.MissingRoundState,
            state.ProjectRound(10, 60, out RoundPresentationState _));
    }
}
