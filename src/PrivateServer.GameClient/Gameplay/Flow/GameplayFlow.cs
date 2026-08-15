using PrivateServer.GameClient.Gameplay.Remote;
using System;

namespace PrivateServer.GameClient.Gameplay.Flow;

internal enum GameplayFlowState
{
    ChannelSelect,
    PlayerSetup,
    Connecting,
    Joining,
    SpawnPending,
    Playing,
    Observing,
    ReturningToChannelSelect,
    Result,
    Error,
    Exiting,
}

internal readonly record struct GameplayFlowObservation(
    RemoteGameplaySessionState SessionState,
    bool IsControlledSpawnPending,
    bool HasControlledEntity,
    bool IsObserver,
    bool HasResult,
    bool HasFault)
{
    internal static GameplayFlowObservation FromSession(RemoteGameplaySession session)
    {
        ArgumentNullException.ThrowIfNull(session);
        return new GameplayFlowObservation(
            session.State,
            session.IsControlledSpawnPending,
            session.ControlledPrediction.HasValue,
            session.Mode == RemoteGameplaySessionMode.Observer,
            session.LatestRoundResult is not null,
            session.LastFault is not null);
    }
}

internal sealed class GameplayFlow
{
    internal GameplayFlowState State { get; private set; } = GameplayFlowState.ChannelSelect;

    internal bool TrySelectChannel()
    {
        if (State != GameplayFlowState.ChannelSelect)
        {
            return false;
        }

        State = GameplayFlowState.PlayerSetup;
        return true;
    }

    internal bool TryBeginConnection()
    {
        if (State != GameplayFlowState.PlayerSetup)
        {
            return false;
        }

        State = GameplayFlowState.Connecting;
        return true;
    }

    internal bool TryBeginObservation()
    {
        if (State != GameplayFlowState.ChannelSelect)
        {
            return false;
        }

        State = GameplayFlowState.Connecting;
        return true;
    }

    internal bool TryApply(GameplayFlowObservation observation)
    {
        if (State == GameplayFlowState.ChannelSelect ||
            State == GameplayFlowState.PlayerSetup ||
            State == GameplayFlowState.Exiting)
        {
            return false;
        }

        if (State == GameplayFlowState.ReturningToChannelSelect)
        {
            if (observation.SessionState == RemoteGameplaySessionState.Idle)
            {
                State = GameplayFlowState.ChannelSelect;
            }
            return true;
        }

        if (observation.HasResult)
        {
            State = GameplayFlowState.Result;
            return true;
        }
        if (observation.HasFault || observation.SessionState == RemoteGameplaySessionState.Faulted)
        {
            State = GameplayFlowState.Error;
            return true;
        }

        switch (observation.SessionState)
        {
            case RemoteGameplaySessionState.Connecting:
                State = GameplayFlowState.Connecting;
                return true;
            case RemoteGameplaySessionState.AwaitingBaseline:
                State = GameplayFlowState.Joining;
                return true;
            case RemoteGameplaySessionState.AwaitingFirstTimeSync:
                State = GameplayFlowState.SpawnPending;
                return true;
            case RemoteGameplaySessionState.Active:
                State = observation.IsObserver
                    ? GameplayFlowState.Observing
                    : observation.IsControlledSpawnPending || !observation.HasControlledEntity
                    ? GameplayFlowState.SpawnPending
                    : GameplayFlowState.Playing;
                return true;
            case RemoteGameplaySessionState.Disconnecting:
                return true;
            case RemoteGameplaySessionState.Idle:
                State = GameplayFlowState.Error;
                return true;
            default:
                return false;
        }
    }

    internal bool TryReturnToChannelSelect()
    {
        if (State == GameplayFlowState.PlayerSetup ||
            State == GameplayFlowState.Result ||
            State == GameplayFlowState.Error)
        {
            State = GameplayFlowState.ChannelSelect;
            return true;
        }
        if (State != GameplayFlowState.Connecting &&
            State != GameplayFlowState.Joining &&
            State != GameplayFlowState.SpawnPending &&
            State != GameplayFlowState.Playing &&
            State != GameplayFlowState.Observing)
        {
            return false;
        }

        State = GameplayFlowState.ReturningToChannelSelect;
        return true;
    }

    internal bool TryExit()
    {
        if (State != GameplayFlowState.ChannelSelect &&
            State != GameplayFlowState.PlayerSetup &&
            State != GameplayFlowState.Result &&
            State != GameplayFlowState.Error)
        {
            return false;
        }

        State = GameplayFlowState.Exiting;
        return true;
    }
}
