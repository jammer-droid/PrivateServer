using Godot;
using PrivateServer.GameClient.Gameplay.Flow;
using PrivateServer.GameClient.Gameplay.Model;
using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Remote;
using System.Collections.Generic;
using System.Text;

using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using RoundResultV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.RoundResult;

namespace PrivateServer.GameClient.Gameplay.Presentation;

public partial class GameplayHud : CanvasLayer
{
    private string channelHeading = "CHANNEL --";
    private Label controlledStateLabel = null!;
    private Label timeLabel = null!;
    private Label leaderboardLabel = null!;
    private GameplayMinimap minimap = null!;
    private PanelContainer activeAreaWarningPanel = null!;
    private AnimationPlayer growthAnimation = null!;
    private PanelContainer resultPanel = null!;
    private Label resultTitleLabel = null!;
    private Label resultDetailsLabel = null!;

    public override void _Ready()
    {
        controlledStateLabel = GetNode<Label>("%ControlledStateLabel");
        timeLabel = GetNode<Label>("%TimeLabel");
        leaderboardLabel = GetNode<Label>("%LeaderboardLabel");
        minimap = GetNode<GameplayMinimap>("%Minimap");
        activeAreaWarningPanel = GetNode<PanelContainer>("%ActiveAreaWarningPanel");
        growthAnimation = GetNode<AnimationPlayer>("%GrowthAnimation");
        resultPanel = GetNode<PanelContainer>("%ResultPanel");
        resultTitleLabel = GetNode<Label>("%ResultTitleLabel");
        resultDetailsLabel = GetNode<Label>("%ResultDetailsLabel");
    }

    internal void ConfigureChannel(GameplayChannelOption channel)
    {
        channelHeading = channel.Name.ToUpperInvariant();
    }

    internal void SetActiveAreaWarning(bool visible)
    {
        activeAreaWarningPanel.Visible = visible;
    }

    internal void PulseGrowth()
    {
        growthAnimation.Play("growth");
    }

    internal void UpdatePresentation(
        RemoteGameplaySession session,
        double nowSeconds,
        RemoteGameplayDrainResult drainResult)
    {
        _ = drainResult;

        ControlledEntityStateV2? controlledState = session.LatestControlledStateV2;
        controlledStateLabel.Text = controlledState is null
            ? "GROWTH --"
            : $"GROWTH {controlledState.GrowthPoint}";

        if (session.TryProjectRound(
                nowSeconds,
                out RoundPresentationState roundPresentation))
        {
            timeLabel.Text =
                $"{channelHeading} · TIME {roundPresentation.RemainingSeconds:0.0}s";
        }
        else
        {
            timeLabel.Text = $"{channelHeading} · TIME --";
        }

        IReadOnlyList<LeaderboardEntry> leaderboard = session.BuildLeaderboard();
        StringBuilder leaderboardText = new StringBuilder();
        if (session.UsesControlProtocolV2 && session.LatestWorldOverview is null)
        {
            leaderboardText.Append("WAITING FOR SERVER OVERVIEW");
        }
        else if (leaderboard.Count == 0)
        {
            leaderboardText.Append('-');
        }
        for (int index = 0; index < leaderboard.Count; ++index)
        {
            LeaderboardEntry entry = leaderboard[index];
            if (leaderboardText.Length > 0)
            {
                leaderboardText.Append('\n');
            }
            string playerLabel = string.IsNullOrEmpty(entry.DisplayName)
                ? $"PLAYER {entry.PlayerId}"
                : entry.DisplayName;
            leaderboardText.Append(
                $"{entry.Rank}. {playerLabel}: {entry.Score}");
        }
        leaderboardLabel.Text = leaderboardText.ToString();

        WorldReady? ready = session.ReadyConfiguration;
        minimap.Apply(session.LatestWorldOverview, ready?.PlayerId);
        ApplyRoundResult(
            session.LatestRoundResult,
            session.LatestRoundResultRecipientPlayerId);
    }

    private void ApplyRoundResult(
        RoundResultV2? result,
        uint? recipientPlayerId)
    {
        resultPanel.Visible = result is not null;
        if (result is null)
        {
            return;
        }

        GameplayResultPresentation presentation =
            GameplayResultPresentation.From(result, recipientPlayerId);
        resultTitleLabel.Text = presentation.Title;
        resultDetailsLabel.Text =
            $"{presentation.Details}\n\nPress R to return to Channel Select";
    }
}
