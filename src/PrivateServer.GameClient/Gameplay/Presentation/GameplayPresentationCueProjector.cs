using PrivateServer.GameClient.Gameplay.Protocol.V2;
using PrivateServer.GameClient.Gameplay.Remote;
using PrivateServer.GameClient.Gameplay.Replication;
using System;
using System.Collections.Generic;

using EntityKindV1 = PrivateServer.GameClient.Gameplay.Protocol.V1.EntityKind;
using EntityRemoveReasonV1 = PrivateServer.GameClient.Gameplay.Protocol.V1.EntityRemoveReason;

namespace PrivateServer.GameClient.Gameplay.Presentation;

internal enum GameplayPresentationCueKind
{
    ControlledSpawn,
    ControlledPresentationReset,
    GrowthChanged,
    BoostStarted,
    BoostStopped,
    RoundResult,
}

internal readonly record struct GameplayPresentationCue(
    GameplayPresentationCueKind Kind,
    bool RecipientWon = false);

internal readonly record struct GameplayPresentationObservation(
    uint TransportGeneration,
    ClientWorldEntityKey? ControlledKey,
    uint? GrowthPoint,
    bool? BoostEnabled,
    uint? RoundId,
    uint? RoundEndTick,
    bool RecipientWon)
{
    internal static GameplayPresentationObservation From(
        RemoteGameplaySession session)
    {
        ArgumentNullException.ThrowIfNull(session);

        ClientWorldEntityKey? controlledKey = null;
        if (session.ReadyConfiguration is not null &&
            session.ControlledPrediction.HasValue)
        {
            controlledKey = new ClientWorldEntityKey(
                session.ReadyConfiguration.ControlledEntityId,
                session.ReadyConfiguration.ControlledEntityGeneration);
        }

        ControlledEntityState? controlledState = session.LatestControlledStateV2;
        uint? growthPoint = controlledState is not null &&
            controlledKey.HasValue &&
            controlledState.ControlledEntityGeneration == controlledKey.Value.Generation
                ? controlledState.GrowthPoint
                : null;
        bool? boostEnabled = controlledState is not null &&
            controlledKey.HasValue &&
            controlledState.ControlledEntityGeneration == controlledKey.Value.Generation
                ? controlledState.BoostState == BoostState.On
                : null;

        RoundResult? result = session.LatestRoundResult;
        bool recipientWon = result is not null &&
            GameplayResultPresentation.IsRecipientWinner(
                result,
                session.LatestRoundResultRecipientPlayerId);
        return new GameplayPresentationObservation(
            session.TransportGeneration,
            controlledKey,
            growthPoint,
            boostEnabled,
            result?.RoundId,
            result?.EndTick,
            recipientWon);
    }
}

internal sealed class GameplayPresentationCueProjector
{
    private uint transportGeneration;
    private bool hasTransportGeneration;
    private ClientWorldEntityKey? controlledKey;
    private uint? growthPoint;
    private bool? boostEnabled;
    private (uint TransportGeneration, uint RoundId, uint EndTick)? resultIdentity;

    internal static bool ShouldPresentRemoval(
        EntityKindV1 entityKind,
        EntityRemoveReasonV1 reason)
    {
        return entityKind != EntityKindV1.Resource ||
            reason != EntityRemoveReasonV1.Destroyed;
    }

    internal IReadOnlyList<GameplayPresentationCue> Project(
        GameplayPresentationObservation observation)
    {
        List<GameplayPresentationCue> cues = new List<GameplayPresentationCue>(4);
        if (!hasTransportGeneration ||
            transportGeneration != observation.TransportGeneration)
        {
            Reset(observation.TransportGeneration);
        }

        if (controlledKey != observation.ControlledKey)
        {
            if (controlledKey.HasValue)
            {
                cues.Add(new GameplayPresentationCue(
                    GameplayPresentationCueKind.ControlledPresentationReset));
            }
            controlledKey = observation.ControlledKey;
            growthPoint = null;
            boostEnabled = null;
            if (controlledKey.HasValue)
            {
                cues.Add(new GameplayPresentationCue(
                    GameplayPresentationCueKind.ControlledSpawn));
            }
        }

        if (controlledKey.HasValue && observation.GrowthPoint.HasValue)
        {
            if (growthPoint.HasValue &&
                growthPoint.Value != observation.GrowthPoint.Value)
            {
                cues.Add(new GameplayPresentationCue(
                    GameplayPresentationCueKind.GrowthChanged));
            }
            growthPoint = observation.GrowthPoint;
        }

        if (controlledKey.HasValue && observation.BoostEnabled.HasValue)
        {
            if (!boostEnabled.HasValue && observation.BoostEnabled.Value)
            {
                cues.Add(new GameplayPresentationCue(
                    GameplayPresentationCueKind.BoostStarted));
            }
            else if (boostEnabled.HasValue &&
                     boostEnabled.Value != observation.BoostEnabled.Value)
            {
                cues.Add(new GameplayPresentationCue(
                    observation.BoostEnabled.Value
                        ? GameplayPresentationCueKind.BoostStarted
                        : GameplayPresentationCueKind.BoostStopped));
            }
            boostEnabled = observation.BoostEnabled;
        }
        else if (boostEnabled == true)
        {
            cues.Add(new GameplayPresentationCue(
                GameplayPresentationCueKind.BoostStopped));
            boostEnabled = null;
        }

        if (observation.RoundId.HasValue && observation.RoundEndTick.HasValue)
        {
            (uint TransportGeneration, uint RoundId, uint EndTick) nextIdentity = (
                observation.TransportGeneration,
                observation.RoundId.Value,
                observation.RoundEndTick.Value);
            if (!resultIdentity.HasValue || resultIdentity.Value != nextIdentity)
            {
                resultIdentity = nextIdentity;
                cues.Add(new GameplayPresentationCue(
                    GameplayPresentationCueKind.RoundResult,
                    observation.RecipientWon));
            }
        }

        return cues.AsReadOnly();
    }

    private void Reset(uint nextTransportGeneration)
    {
        transportGeneration = nextTransportGeneration;
        hasTransportGeneration = true;
        controlledKey = null;
        growthPoint = null;
        boostEnabled = null;
        resultIdentity = null;
    }
}
