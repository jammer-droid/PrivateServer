using PrivateServer.GameClient.Gameplay.Protocol.V1;
using PrivateServer.GameClient.Gameplay.Replication;
using System.Collections.Generic;

namespace PrivateServer.GameClient.Gameplay.Remote;

internal enum RemoteGameplaySessionState
{
    Idle,
    Connecting,
    AwaitingBaseline,
    AwaitingFirstTimeSync,
    Active,
    Disconnecting,
    Faulted,
}

internal enum RemoteGameplaySessionFaultKind
{
    TransportFailure,
    SendFailure,
    ProtocolDecode,
    PacketOrdering,
    TimeSync,
    Clock,
    Prediction,
    Replication,
    GameplayState,
}

internal sealed record RemoteGameplaySessionFault(
    RemoteGameplaySessionFaultKind Kind,
    uint TransportGeneration,
    uint? PacketType,
    string Message);

internal readonly record struct RemoteGameplayRemovalNotice(
    uint ServerTick,
    ClientWorldEntityKey Key,
    EntityRemoveReason Reason);

internal readonly record struct RemoteGameplayDrainResult(
    int DrainedEventCount,
    bool ReachedBudget,
    RemoteGameplaySessionState State,
    IReadOnlyList<RemoteGameplayRemovalNotice> RemovalNotices);

internal enum RemoteGameplaySessionOperationError
{
    None,
    InvalidArgument,
    InvalidState,
    TransportFailure,
    SessionFault,
    Disposed,
}

internal readonly record struct RemoteGameplaySessionOperationResult(
    RemoteGameplaySessionOperationError Error,
    RemoteGameplayTransportStatus? TransportStatus)
{
    internal bool Succeeded => Error == RemoteGameplaySessionOperationError.None;
}
