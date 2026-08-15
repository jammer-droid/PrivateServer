using PrivateServer.GameClient.Gameplay.Protocol.V2;

namespace PrivateServer.GameClient.Gameplay.Remote;

internal enum ControlStateUpdateResult
{
    CommandCreated,
    Unchanged,
    InvalidGeneration,
    SequenceExhausted,
}

internal enum ControlAcknowledgementResult
{
    Accepted,
    Stale,
    BeyondSentSequence,
}

internal sealed class BoostInputGate
{
    private bool releaseRequired;

    internal bool Resolve(bool boostHeld, uint growthPoint)
    {
        if (!boostHeld)
        {
            releaseRequired = false;
            return false;
        }
        if (releaseRequired)
        {
            return false;
        }
        if (growthPoint == 0)
        {
            releaseRequired = true;
            return false;
        }
        return true;
    }

    internal void Reset()
    {
        releaseRequired = false;
    }
}

internal sealed class ControlStateTracker
{
    private bool hasLastSentState;
    private TurnState lastSentTurnState;
    private BoostState lastSentBoostState;

    internal uint LastSentSequence { get; private set; }
    internal uint LastAcknowledgedSequence { get; private set; }

    internal ControlStateUpdateResult TryCreateCommand(
        uint controlledEntityGeneration,
        bool leftHeld,
        bool rightHeld,
        bool boostHeld,
        out ControlStateCommand command)
    {
        command = default;
        if (controlledEntityGeneration == 0)
        {
            return ControlStateUpdateResult.InvalidGeneration;
        }

        TurnState turnState = ResolveTurnState(leftHeld, rightHeld);
        BoostState boostState = boostHeld ? BoostState.On : BoostState.Off;
        if (hasLastSentState &&
            turnState == lastSentTurnState &&
            boostState == lastSentBoostState)
        {
            return ControlStateUpdateResult.Unchanged;
        }
        if (LastSentSequence == uint.MaxValue)
        {
            return ControlStateUpdateResult.SequenceExhausted;
        }

        ++LastSentSequence;
        lastSentTurnState = turnState;
        lastSentBoostState = boostState;
        hasLastSentState = true;
        command = new ControlStateCommand(
            controlledEntityGeneration,
            LastSentSequence,
            turnState,
            boostState);
        return ControlStateUpdateResult.CommandCreated;
    }

    internal ControlAcknowledgementResult AcceptAcknowledgement(uint sequence)
    {
        if (sequence > LastSentSequence)
        {
            return ControlAcknowledgementResult.BeyondSentSequence;
        }
        if (sequence < LastAcknowledgedSequence)
        {
            return ControlAcknowledgementResult.Stale;
        }

        LastAcknowledgedSequence = sequence;
        return ControlAcknowledgementResult.Accepted;
    }

    internal void Reset()
    {
        hasLastSentState = false;
        lastSentTurnState = TurnState.Invalid;
        lastSentBoostState = BoostState.Invalid;
        LastSentSequence = 0;
        LastAcknowledgedSequence = 0;
    }

    internal static TurnState ResolveTurnState(bool leftHeld, bool rightHeld)
    {
        if (leftHeld)
        {
            return TurnState.Left;
        }
        if (rightHeld)
        {
            return TurnState.Right;
        }
        return TurnState.Straight;
    }
}
