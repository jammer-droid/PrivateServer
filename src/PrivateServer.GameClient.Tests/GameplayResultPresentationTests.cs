using Microsoft.VisualStudio.TestTools.UnitTesting;
using PrivateServer.GameClient.Gameplay.Model;
using PrivateServer.GameClient.Gameplay.Presentation;
using PrivateServer.GameClient.Gameplay.Protocol.V2;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayResultPresentationTests
{
    [TestMethod]
    public void ObserverResultOmitsRecipientSpecificGrowth()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 12, 0, new uint[] { 7 }),
            recipientPlayerId: null);

        Assert.AreEqual("ROUND COMPLETE", presentation.Title);
        Assert.IsFalse(presentation.Details.Contains("Final growth", System.StringComparison.Ordinal));
        StringAssert.Contains(presentation.Details, "Winning growth: 12");
    }

    [TestMethod]
    public void MarksRecipientVictoryForSingleWinner()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 12, 12, new uint[] { 7 }),
            recipientPlayerId: 7);

        Assert.AreEqual("VICTORY", presentation.Title);
        StringAssert.Contains(presentation.Details, "Final growth: 12");
        StringAssert.Contains(presentation.Details, "Winning growth: 12");
        StringAssert.Contains(presentation.Details, "Winner: player 7");
    }

    [TestMethod]
    public void ListsAllCoWinners()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 12, 8, new uint[] { 7, 11 }),
            recipientPlayerId: 5);

        Assert.AreEqual("CO-WINNERS", presentation.Title);
        StringAssert.Contains(presentation.Details, "Co-winners: player 7, player 11");
    }

    [TestMethod]
    public void DisplaysWinnerNamesFromTheFinalLeaderboard()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 12, 8, new uint[] { 7, 11 }),
            recipientPlayerId: 5,
            new LeaderboardEntry[]
            {
                new LeaderboardEntry(1, 7, 12, 100, "Alice"),
                new LeaderboardEntry(2, 11, 12, 100, "Bob"),
            });

        StringAssert.Contains(presentation.Details, "Co-winners: Alice, Bob");
        Assert.IsFalse(presentation.Details.Contains("player 7"));
        Assert.IsFalse(presentation.Details.Contains("player 11"));
    }

    [TestMethod]
    public void ShowsNoWinnerWhenWinnerListIsEmpty()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 0, 8, System.Array.Empty<uint>()),
            recipientPlayerId: 7);

        Assert.AreEqual("NO WINNER", presentation.Title);
        StringAssert.Contains(presentation.Details, "Winner: none");
        Assert.IsFalse(presentation.Details.Contains("Winning growth:"));
    }

    [TestMethod]
    public void MarksRecipientCoVictoryWhenTiedAtWinningGrowth()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 12, 12, new uint[] { 7, 11 }),
            recipientPlayerId: 11);

        Assert.AreEqual("CO-VICTORY", presentation.Title);
        StringAssert.Contains(presentation.Details, "Co-winners: player 7, player 11");
    }

    [TestMethod]
    public void EqualGrowthDoesNotOverrideAuthoritativeWinnerMembership()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(100, 3, 0, 0, new uint[] { 7 }),
            recipientPlayerId: 11);

        Assert.AreEqual("ROUND COMPLETE", presentation.Title);
    }

    [TestMethod]
    public void SummarizesLargeCoWinnerListsWithinTheResultPanel()
    {
        GameplayResultPresentation presentation = GameplayResultPresentation.From(
            new RoundResult(
                100,
                3,
                12,
                8,
                new uint[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }),
            recipientPlayerId: 11);

        StringAssert.Contains(
            presentation.Details,
            "Co-winners: player 1, player 2, player 3, player 4, player 5, player 6, player 7, player 8, +2 more");
        Assert.IsFalse(presentation.Details.Contains("player 9"));
    }
}
