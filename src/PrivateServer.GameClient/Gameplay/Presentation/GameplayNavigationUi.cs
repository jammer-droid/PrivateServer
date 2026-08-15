using Godot;
using PrivateServer.GameClient.Gameplay.Flow;
using PrivateServer.GameClient.Gameplay.Protocol;
using System;
using System.Collections.Generic;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class GameplayNavigationUi : CanvasLayer
{
    private PanelContainer channelPanel = null!;
    private OptionButton channelSelector = null!;
    private Button connectButton = null!;
    private PanelContainer playerSetupPanel = null!;
    private Label selectedChannelLabel = null!;
    private LineEdit nicknameInput = null!;
    private Button playerConnectButton = null!;
    private PanelContainer statusPanel = null!;
    private Label statusLabel = null!;
    private PanelContainer errorPanel = null!;
    private Label errorLabel = null!;
    private PanelContainer resultActionsPanel = null!;
    private AnimationPlayer transitionAnimation = null!;
    private GameplayFlowState? appliedState;

    internal event Action<int>? ChannelSelected;
    internal event Action<string>? ConnectRequested;
    internal event Action? ReturnToChannelSelectRequested;
    internal event Action? ExitRequested;

    public override void _Ready()
    {
        channelPanel = GetNode<PanelContainer>("%ChannelPanel");
        channelSelector = GetNode<OptionButton>("%ChannelSelector");
        connectButton = GetNode<Button>("%ConnectButton");
        playerSetupPanel = GetNode<PanelContainer>("%PlayerSetupPanel");
        selectedChannelLabel = GetNode<Label>("%SelectedChannelLabel");
        nicknameInput = GetNode<LineEdit>("%NicknameInput");
        playerConnectButton = GetNode<Button>("%PlayerConnectButton");
        statusPanel = GetNode<PanelContainer>("%StatusPanel");
        statusLabel = GetNode<Label>("%StatusLabel");
        errorPanel = GetNode<PanelContainer>("%ErrorPanel");
        errorLabel = GetNode<Label>("%ErrorLabel");
        resultActionsPanel = GetNode<PanelContainer>("%ResultActionsPanel");
        transitionAnimation = GetNode<AnimationPlayer>("%TransitionAnimation");

        connectButton.Pressed += () => ChannelSelected?.Invoke(channelSelector.Selected);
        playerConnectButton.Pressed += () => ConnectRequested?.Invoke(nicknameInput.Text);
        nicknameInput.TextChanged += FilterNicknameInput;
        GetNode<Button>("%PlayerBackButton").Pressed +=
            () => ReturnToChannelSelectRequested?.Invoke();
        GetNode<Button>("%ChannelExitButton").Pressed +=
            () => ExitRequested?.Invoke();
        GetNode<Button>("%RetryButton").Pressed +=
            () => ReturnToChannelSelectRequested?.Invoke();
        GetNode<Button>("%ErrorExitButton").Pressed +=
            () => ExitRequested?.Invoke();
        GetNode<Button>("%PlayAgainButton").Pressed +=
            () => ReturnToChannelSelectRequested?.Invoke();
        GetNode<Button>("%ResultExitButton").Pressed +=
            () => ExitRequested?.Invoke();
    }

    internal void ConfigureChannels(IReadOnlyList<GameplayChannelOption> channels)
    {
        channelSelector.Clear();
        for (int index = 0; index < channels.Count; ++index)
        {
            GameplayChannelOption channel = channels[index];
            channelSelector.AddItem(
                $"{channel.Name}  ·  {channel.Address}:{channel.Port}");
        }
        if (channelSelector.ItemCount > 0)
        {
            channelSelector.Select(0);
        }
    }

    internal void ConfigureSelectedChannel(GameplayChannelOption channel)
    {
        selectedChannelLabel.Text =
            $"{channel.Name}\n{channel.Address}:{channel.Port}";
        nicknameInput.Text = string.Empty;
        nicknameInput.GrabFocus();
    }

    internal void ResetPlayerSetup()
    {
        nicknameInput.Text = string.Empty;
    }

    internal void Apply(
        GameplayFlowState state,
        string? faultMessage,
        bool canConnect)
    {
        bool stateChanged = !appliedState.HasValue || appliedState.Value != state;
        appliedState = state;
        channelPanel.Visible = state == GameplayFlowState.ChannelSelect;
        playerSetupPanel.Visible = state == GameplayFlowState.PlayerSetup;
        statusPanel.Visible =
            state == GameplayFlowState.Connecting ||
            state == GameplayFlowState.Joining ||
            state == GameplayFlowState.SpawnPending;
        errorPanel.Visible = state == GameplayFlowState.Error;
        resultActionsPanel.Visible = state == GameplayFlowState.Result;

        connectButton.Disabled = !canConnect || channelSelector.ItemCount == 0;
        connectButton.Text = canConnect
            ? "NEXT"
            : "CLOSING PREVIOUS SESSION...";
        playerConnectButton.Disabled = !canConnect;
        statusLabel.Text = state switch
        {
            GameplayFlowState.Connecting => "LINKING TO WORLD SERVER",
            GameplayFlowState.Joining => "SYNCHRONIZING AUTHORITATIVE WORLD",
            GameplayFlowState.SpawnPending => "WAITING FOR SERVER SPAWN",
            _ => string.Empty,
        };
        errorLabel.Text = string.IsNullOrWhiteSpace(faultMessage)
            ? "The World connection ended before a result was committed."
            : faultMessage;
        if (stateChanged)
        {
            transitionAnimation.Play("enter");
        }
    }

    private void FilterNicknameInput(string value)
    {
        if (PlayerDisplayNameRules.IsValid(value))
        {
            return;
        }

        string filtered = string.Empty;
        for (int index = 0;
             index < value.Length && filtered.Length < PlayerDisplayNameRules.MaximumByteCount;
             ++index)
        {
            char character = value[index];
            if ((character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9'))
            {
                filtered += character;
            }
        }
        nicknameInput.Text = filtered;
        nicknameInput.CaretColumn = filtered.Length;
    }
}
