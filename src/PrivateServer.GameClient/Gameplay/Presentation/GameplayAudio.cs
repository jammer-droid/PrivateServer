using Godot;
using PrivateServer.GameClient.Gameplay.Flow;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal partial class GameplayAudio : Node
{
    private const float SilentVolumeDb = -40.0f;
    private const float MusicVolumeDb = 0.0f;
    private const double MusicFadeSeconds = 0.55;
    private const double WarningCooldownSeconds = 1.5;

    private AudioStreamPlayer lobbyMusic = null!;
    private AudioStreamPlayer playingMusic = null!;
    private AudioStreamPlayer resultMusic = null!;
    private AudioStreamPlayer hoverPlayer = null!;
    private AudioStreamPlayer clickPlayer = null!;
    private AudioStreamPlayer overlayPlayer = null!;
    private AudioStreamPlayer confirmationPlayer = null!;
    private AudioStreamPlayer errorPlayer = null!;
    private AudioStreamPlayer spawnPlayer = null!;
    private AudioStreamPlayer growthPlayer = null!;
    private AudioStreamPlayer collectPlayer = null!;
    private AudioStreamPlayer destroyedPlayer = null!;
    private AudioStreamPlayer warningPlayer = null!;
    private AudioStreamPlayer victoryPlayer = null!;
    private AudioStreamPlayer boostLoopPlayer = null!;
    private AudioStreamPlayer? activeMusic;
    private GameplayFlowState? previousFlowState;
    private Tween? musicTween;
    private double lastWarningSeconds = double.NegativeInfinity;
    private bool playbackEnabled;

    public override void _Ready()
    {
        lobbyMusic = GetNode<AudioStreamPlayer>("%LobbyMusic");
        playingMusic = GetNode<AudioStreamPlayer>("%PlayingMusic");
        resultMusic = GetNode<AudioStreamPlayer>("%ResultMusic");
        hoverPlayer = GetNode<AudioStreamPlayer>("%HoverPlayer");
        clickPlayer = GetNode<AudioStreamPlayer>("%ClickPlayer");
        overlayPlayer = GetNode<AudioStreamPlayer>("%OverlayPlayer");
        confirmationPlayer = GetNode<AudioStreamPlayer>("%ConfirmationPlayer");
        errorPlayer = GetNode<AudioStreamPlayer>("%ErrorPlayer");
        spawnPlayer = GetNode<AudioStreamPlayer>("%SpawnPlayer");
        growthPlayer = GetNode<AudioStreamPlayer>("%GrowthPlayer");
        collectPlayer = GetNode<AudioStreamPlayer>("%CollectPlayer");
        destroyedPlayer = GetNode<AudioStreamPlayer>("%DestroyedPlayer");
        warningPlayer = GetNode<AudioStreamPlayer>("%WarningPlayer");
        victoryPlayer = GetNode<AudioStreamPlayer>("%VictoryPlayer");
        boostLoopPlayer = GetNode<AudioStreamPlayer>("%BoostLoopPlayer");
        playbackEnabled = DisplayServer.GetName() != "headless";
    }

    internal void BindButtons(Node root)
    {
        if (root is BaseButton button)
        {
            button.MouseEntered += PlayUiHover;
            button.Pressed += PlayUiClick;
        }

        foreach (Node child in root.GetChildren())
        {
            BindButtons(child);
        }
    }

    internal void ApplyFlow(GameplayFlowState state)
    {
        if (previousFlowState == state)
        {
            return;
        }

        GameplayFlowState? previous = previousFlowState;
        previousFlowState = state;
        if (!playbackEnabled)
        {
            return;
        }
        AudioStreamPlayer? nextMusic = state switch
        {
            GameplayFlowState.ChannelSelect => lobbyMusic,
            GameplayFlowState.PlayerSetup => lobbyMusic,
            GameplayFlowState.Connecting => lobbyMusic,
            GameplayFlowState.Joining => lobbyMusic,
            GameplayFlowState.SpawnPending => playingMusic,
            GameplayFlowState.Playing => playingMusic,
            GameplayFlowState.Result => resultMusic,
            GameplayFlowState.Error => lobbyMusic,
            GameplayFlowState.Exiting => null,
            _ => null,
        };
        TransitionMusic(nextMusic);

        if (state == GameplayFlowState.Error)
        {
            PlayOneShot(errorPlayer);
        }
        else if (state == GameplayFlowState.Playing &&
                 previous.HasValue &&
                 previous.Value != GameplayFlowState.Playing)
        {
            PlayOneShot(confirmationPlayer);
        }

        if (state != GameplayFlowState.Playing)
        {
            SetBoost(false);
        }
    }

    internal void PlayOverlayToggle()
    {
        PlayOneShot(overlayPlayer);
    }

    internal void PlaySpawn()
    {
        PlayOneShot(spawnPlayer);
    }

    internal void PlayGrowth()
    {
        PlayOneShot(growthPlayer);
    }

    internal void PlayCollected()
    {
        PlayOneShot(collectPlayer);
    }

    internal void PlayDestroyed()
    {
        PlayOneShot(destroyedPlayer);
    }

    internal void PlayWarning(double nowSeconds)
    {
        if (!playbackEnabled ||
            !double.IsFinite(nowSeconds) ||
            nowSeconds - lastWarningSeconds < WarningCooldownSeconds)
        {
            return;
        }

        lastWarningSeconds = nowSeconds;
        PlayOneShot(warningPlayer);
    }

    internal void PlayResult(bool victory)
    {
        if (victory)
        {
            PlayOneShot(victoryPlayer);
        }
        else
        {
            PlayOneShot(confirmationPlayer);
        }
    }

    internal void SetBoost(bool enabled)
    {
        if (!playbackEnabled)
        {
            return;
        }

        if (enabled)
        {
            if (!boostLoopPlayer.Playing)
            {
                boostLoopPlayer.Play();
            }
            return;
        }

        boostLoopPlayer.Stop();
    }

    public override void _ExitTree()
    {
        musicTween?.Kill();
        musicTween = null;
        activeMusic = null;
        StopAndRelease(lobbyMusic);
        StopAndRelease(playingMusic);
        StopAndRelease(resultMusic);
        StopAndRelease(boostLoopPlayer);
    }

    private void PlayUiHover()
    {
        PlayOneShot(hoverPlayer);
    }

    private void PlayUiClick()
    {
        PlayOneShot(clickPlayer);
    }

    private void TransitionMusic(AudioStreamPlayer? nextMusic)
    {
        if (ReferenceEquals(activeMusic, nextMusic))
        {
            return;
        }

        musicTween?.Kill();
        AudioStreamPlayer? outgoingMusic = activeMusic;
        StopInactiveMusic(outgoingMusic, nextMusic);
        activeMusic = nextMusic;

        if (nextMusic is not null)
        {
            nextMusic.VolumeDb = SilentVolumeDb;
            if (!nextMusic.Playing)
            {
                nextMusic.Play();
            }
        }

        musicTween = CreateTween();
        musicTween.SetParallel(true);
        if (nextMusic is not null)
        {
            musicTween.TweenProperty(
                nextMusic,
                "volume_db",
                MusicVolumeDb,
                MusicFadeSeconds);
        }
        if (outgoingMusic is not null)
        {
            musicTween.TweenProperty(
                outgoingMusic,
                "volume_db",
                SilentVolumeDb,
                MusicFadeSeconds);
        }
        musicTween.SetParallel(false);
        if (outgoingMusic is not null)
        {
            musicTween.TweenCallback(Callable.From(outgoingMusic.Stop));
        }
    }

    private void StopInactiveMusic(
        AudioStreamPlayer? outgoingMusic,
        AudioStreamPlayer? nextMusic)
    {
        StopIfInactive(lobbyMusic, outgoingMusic, nextMusic);
        StopIfInactive(playingMusic, outgoingMusic, nextMusic);
        StopIfInactive(resultMusic, outgoingMusic, nextMusic);
    }

    private static void StopIfInactive(
        AudioStreamPlayer player,
        AudioStreamPlayer? outgoingMusic,
        AudioStreamPlayer? nextMusic)
    {
        if (!ReferenceEquals(player, outgoingMusic) &&
            !ReferenceEquals(player, nextMusic))
        {
            player.Stop();
        }
    }

    private static void StopAndRelease(AudioStreamPlayer player)
    {
        player.Stop();
        player.Stream = null;
    }

    private void PlayOneShot(AudioStreamPlayer player)
    {
        if (playbackEnabled)
        {
            player.Play();
        }
    }
}
