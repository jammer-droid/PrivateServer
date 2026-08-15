using PrivateServer.GameClient.Gameplay.Protocol.V1;
using System;
using System.Collections.Generic;
using System.Numerics;

using EntityStateRecordV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateRecord;

namespace PrivateServer.GameClient.Gameplay.Replication;

internal enum RemoteReplicaSpawnOutcome
{
    Added,
    ResetSameGeneration,
    ReplacedGeneration,
}

internal enum RemoteReplicaRemoveOutcome
{
    Removed,
    MissingIgnored,
}

internal enum RemoteReplicaStoreError
{
    None,
    InvalidArgument,
    MissingReplica,
    DuplicateBatchRecord,
    StaticReplicaState,
    NonPlayerWholeBodyState,
    NonIncreasingServerTick,
    MissingWholeBodyState,
}

internal readonly record struct RemoteReplicaPresentationSnapshot(
    ClientWorldEntityKey Key,
    EntityKind EntityKind,
    float Radius,
    Vector2 HeadPosition,
    float HeadingRadians,
    RemoteWholeBodyPresentationSample? WholeBody);

internal readonly record struct RemoteWholeBodySnapshot(
    uint ServerTick,
    uint SnapshotId,
    EntityStateRecordV2 State);

internal sealed class RemoteReplica
{
    internal RemoteReplica(EntitySpawn spawn)
    {
        Spawn = spawn;
        Key = new ClientWorldEntityKey(spawn.EntityId, spawn.Generation);
        History = new RemoteSnapshotHistory();
        WholeBodyHistory = new RemoteWholeBodySnapshotHistory();
        Reset(spawn);
    }

    internal ClientWorldEntityKey Key { get; }
    internal EntitySpawn Spawn { get; private set; }
    internal EntityKind EntityKind { get; private set; }
    internal uint ArchetypeId { get; private set; }
    internal ShapeKind PrimaryShapeKind { get; private set; }
    internal float PrimaryCircleRadius { get; private set; }
    internal float MaxMoveSpeed { get; private set; }
    internal RemoteSnapshotHistory History { get; }
    internal RemoteWholeBodySnapshotHistory WholeBodyHistory { get; }
    internal RemoteWholeBodySnapshot? LatestWholeBodySnapshot => WholeBodyHistory.LatestSnapshot;

    internal void Reset(EntitySpawn spawn)
    {
        Spawn = spawn;
        EntityKind = spawn.EntityKind;
        ArchetypeId = spawn.ArchetypeId;
        PrimaryShapeKind = spawn.PrimaryShapeKind;
        PrimaryCircleRadius = spawn.PrimaryCircleRadius;
        MaxMoveSpeed = spawn.MaxMoveSpeed;
        History.Reset(ToSnapshot(spawn));
        WholeBodyHistory.Clear();
    }

    internal bool CanApplyState(uint serverTick)
    {
        if (!History.LatestServerTick.HasValue)
        {
            return true;
        }

        uint latestServerTick = History.LatestServerTick.Value;
        return serverTick >= latestServerTick;
    }

    internal RemoteSnapshotHistoryError ApplyState(
        uint serverTick,
        EntityStateRecord state)
    {
        RemoteAuthoritativeSnapshot snapshot = ToSnapshot(serverTick, state);
        if (History.LatestServerTick == serverTick)
        {
            return History.ReplaceLatest(snapshot);
        }

        return History.Add(snapshot);
    }

    internal bool CanApplyWholeBodyState(uint serverTick)
    {
        return WholeBodyHistory.CanApply(serverTick, Spawn.ServerTick);
    }

    internal void ApplyWholeBodyState(
        uint serverTick,
        uint snapshotId,
        EntityStateRecordV2 state)
    {
        WholeBodyHistory.AddOrReplace(new RemoteWholeBodySnapshot(
            serverTick,
            snapshotId,
            state));
    }

    internal static RemoteAuthoritativeSnapshot ToSnapshot(EntitySpawn spawn)
    {
        return new RemoteAuthoritativeSnapshot(
            spawn.ServerTick,
            new Vector2(spawn.PositionX, spawn.PositionY),
            new Vector2(spawn.VelocityX, spawn.VelocityY),
            spawn.AngleRadians);
    }

    internal static RemoteAuthoritativeSnapshot ToSnapshot(
        uint serverTick,
        EntityStateRecord state)
    {
        return new RemoteAuthoritativeSnapshot(
            serverTick,
            new Vector2(state.PositionX, state.PositionY),
            new Vector2(state.VelocityX, state.VelocityY),
            state.AngleRadians);
    }
}

internal sealed class RemoteReplicaStore
{
    private readonly Dictionary<ClientWorldEntityKey, RemoteReplica> replicas = new();
    private readonly uint tickRateHz;
    private readonly uint interpolationDelayTicks;
    private readonly uint maxExtrapolationTicks;

    internal RemoteReplicaStore(uint tickRateHz, uint snapshotIntervalTicks)
    {
        ulong doubledInterval = (ulong)snapshotIntervalTicks * 2UL;
        if (tickRateHz == 0 ||
            snapshotIntervalTicks == 0 ||
            doubledInterval > uint.MaxValue)
        {
            throw new ArgumentOutOfRangeException(
                nameof(snapshotIntervalTicks),
                "Replica timing configuration is outside the supported tick range.");
        }

        this.tickRateHz = tickRateHz;
        interpolationDelayTicks = checked((uint)Math.Max(2UL, doubledInterval));
        maxExtrapolationTicks = checked((uint)Math.Max(1UL, doubledInterval));
    }

    internal int Count => replicas.Count;
    internal uint InterpolationDelayTicks => interpolationDelayTicks;
    internal uint MaxExtrapolationTicks => maxExtrapolationTicks;

    internal RemoteReplicaStoreError ApplySpawn(
        EntitySpawn spawn,
        out RemoteReplicaSpawnOutcome outcome)
    {
        ArgumentNullException.ThrowIfNull(spawn);
        ClientWorldEntityKey key =
            new ClientWorldEntityKey(spawn.EntityId, spawn.Generation);
        if (!key.IsValid)
        {
            outcome = default;
            return RemoteReplicaStoreError.InvalidArgument;
        }

        if (replicas.TryGetValue(key, out RemoteReplica? sameGeneration))
        {
            sameGeneration.Reset(spawn);
            outcome = RemoteReplicaSpawnOutcome.ResetSameGeneration;
            return RemoteReplicaStoreError.None;
        }

        bool replacedGeneration = false;
        List<ClientWorldEntityKey> staleKeys = new List<ClientWorldEntityKey>();
        foreach (ClientWorldEntityKey existingKey in replicas.Keys)
        {
            if (existingKey.EntityId == key.EntityId)
            {
                staleKeys.Add(existingKey);
            }
        }
        for (int index = 0; index < staleKeys.Count; ++index)
        {
            replicas.Remove(staleKeys[index]);
            replacedGeneration = true;
        }

        replicas.Add(key, new RemoteReplica(spawn));
        outcome = replacedGeneration
            ? RemoteReplicaSpawnOutcome.ReplacedGeneration
            : RemoteReplicaSpawnOutcome.Added;
        return RemoteReplicaStoreError.None;
    }

    internal RemoteReplicaStoreError ApplyStateBatch(EntityStateBatch batch)
    {
        ArgumentNullException.ThrowIfNull(batch);
        HashSet<ClientWorldEntityKey> batchKeys = new HashSet<ClientWorldEntityKey>();
        for (int index = 0; index < batch.Records.Count; ++index)
        {
            EntityStateRecord state = batch.Records[index];
            ClientWorldEntityKey key =
                new ClientWorldEntityKey(state.EntityId, state.Generation);
            if (!batchKeys.Add(key))
            {
                return RemoteReplicaStoreError.DuplicateBatchRecord;
            }
            if (!replicas.TryGetValue(key, out RemoteReplica? replica))
            {
                return RemoteReplicaStoreError.MissingReplica;
            }
            if (replica.EntityKind == EntityKind.StaticObstacle)
            {
                return RemoteReplicaStoreError.StaticReplicaState;
            }
            if (!replica.CanApplyState(batch.ServerTick))
            {
                return RemoteReplicaStoreError.NonIncreasingServerTick;
            }
        }

        for (int index = 0; index < batch.Records.Count; ++index)
        {
            EntityStateRecord state = batch.Records[index];
            ClientWorldEntityKey key =
                new ClientWorldEntityKey(state.EntityId, state.Generation);
            RemoteReplica replica = replicas[key];
            RemoteSnapshotHistoryError historyError =
                replica.ApplyState(batch.ServerTick, state);
            if (historyError != RemoteSnapshotHistoryError.None)
            {
                return RemoteReplicaStoreError.NonIncreasingServerTick;
            }
        }

        return RemoteReplicaStoreError.None;
    }

    internal RemoteReplicaStoreError ApplyRemove(
        EntityRemove remove,
        out RemoteReplicaRemoveOutcome outcome)
    {
        ArgumentNullException.ThrowIfNull(remove);
        ClientWorldEntityKey key =
            new ClientWorldEntityKey(remove.EntityId, remove.Generation);
        if (!key.IsValid)
        {
            outcome = default;
            return RemoteReplicaStoreError.InvalidArgument;
        }

        outcome = replicas.Remove(key)
            ? RemoteReplicaRemoveOutcome.Removed
            : RemoteReplicaRemoveOutcome.MissingIgnored;
        return RemoteReplicaStoreError.None;
    }

    internal RemoteReplicaStoreError ApplyStateGroup(RemoteEntityStateGroup group)
    {
        ArgumentNullException.ThrowIfNull(group);
        for (int index = 0; index < group.Records.Count; ++index)
        {
            EntityStateRecordV2 state = group.Records[index];
            ClientWorldEntityKey key =
                new ClientWorldEntityKey(state.EntityId, state.Generation);
            if (!replicas.TryGetValue(key, out RemoteReplica? replica))
            {
                return RemoteReplicaStoreError.MissingReplica;
            }
            if (replica.EntityKind != EntityKind.Player)
            {
                return RemoteReplicaStoreError.NonPlayerWholeBodyState;
            }
            if (!replica.CanApplyWholeBodyState(group.ServerTick))
            {
                return RemoteReplicaStoreError.NonIncreasingServerTick;
            }
        }

        for (int index = 0; index < group.Records.Count; ++index)
        {
            EntityStateRecordV2 state = group.Records[index];
            ClientWorldEntityKey key =
                new ClientWorldEntityKey(state.EntityId, state.Generation);
            replicas[key].ApplyWholeBodyState(
                group.ServerTick,
                group.SnapshotId,
                state);
        }
        return RemoteReplicaStoreError.None;
    }

    // 최초 baseline에서는 WorldReady가 controlled key를 알려주므로 해당 EntitySpawn을
    // 이 store에 넣지 않고 바로 ControlledEntityPrediction으로 만들 수 있다. 반면
    // Active 상태의 EntitySpawn만으로는 다른 AOI entity인지 새 controlled entity인지
    // 구분할 수 없으므로 client 내부 RemoteReplica로 먼저 staging한다. 이어지는
    // ControlledEntityRebind가 소유권을 확정하면 이 store에서 제거하면서 spawn
    // 초기 상태를 반환하고, caller가 ControlledEntityPrediction으로 승격한다.
    // 서버가 별도의 replica 객체를 전송하거나 이 store를 직접 관리하는 것은 아니다.
    internal RemoteReplicaStoreError TryTakeForControl(
        ClientWorldEntityKey key,
        out EntitySpawn? spawn)
    {
        spawn = null;
        if (!key.IsValid)
        {
            return RemoteReplicaStoreError.InvalidArgument;
        }
        if (!replicas.Remove(key, out RemoteReplica? replica))
        {
            return RemoteReplicaStoreError.MissingReplica;
        }

        spawn = replica.Spawn;
        return RemoteReplicaStoreError.None;
    }

    internal bool Contains(ClientWorldEntityKey key)
    {
        return replicas.ContainsKey(key);
    }

    internal bool TryGetLatestWholeBodySnapshot(
        ClientWorldEntityKey key,
        out RemoteWholeBodySnapshot snapshot)
    {
        snapshot = default;
        if (!replicas.TryGetValue(key, out RemoteReplica? replica) ||
            !replica.LatestWholeBodySnapshot.HasValue)
        {
            return false;
        }

        snapshot = replica.LatestWholeBodySnapshot.Value;
        return true;
    }

    internal RemoteReplicaStoreError SampleWholeBody(
        ClientWorldEntityKey key,
        double estimatedServerTimeline,
        out RemoteWholeBodyPresentationSample? sample)
    {
        sample = null;
        if (!key.IsValid ||
            !double.IsFinite(estimatedServerTimeline) ||
            estimatedServerTimeline < 0.0)
        {
            return RemoteReplicaStoreError.InvalidArgument;
        }
        if (!replicas.TryGetValue(key, out RemoteReplica? replica))
        {
            return RemoteReplicaStoreError.MissingReplica;
        }

        double targetServerTick = Math.Max(
            0.0,
            estimatedServerTimeline - interpolationDelayTicks);
        return replica.WholeBodyHistory.TrySample(targetServerTick, out sample)
            ? RemoteReplicaStoreError.None
            : RemoteReplicaStoreError.MissingWholeBodyState;
    }

    internal int CountByKind(EntityKind entityKind)
    {
        int count = 0;
        foreach (RemoteReplica replica in replicas.Values)
        {
            if (replica.EntityKind == entityKind)
            {
                ++count;
            }
        }

        return count;
    }

    internal RemoteReplicaStoreError BuildPresentationSnapshots(
        double estimatedServerTimeline,
        out IReadOnlyList<RemoteReplicaPresentationSnapshot> snapshots)
    {
        List<RemoteReplicaPresentationSnapshot> built =
            new List<RemoteReplicaPresentationSnapshot>(replicas.Count);
        foreach (KeyValuePair<ClientWorldEntityKey, RemoteReplica> pair in replicas)
        {
            Vector2 headPosition;
            float headingRadians;
            RemoteWholeBodyPresentationSample? wholeBody = null;
            float radius = pair.Value.PrimaryCircleRadius;
            if (pair.Value.LatestWholeBodySnapshot.HasValue)
            {
                double targetServerTick = Math.Max(
                    0.0,
                    estimatedServerTimeline - interpolationDelayTicks);
                if (!pair.Value.WholeBodyHistory.TrySample(targetServerTick, out wholeBody) ||
                    wholeBody is null)
                {
                    snapshots = Array.Empty<RemoteReplicaPresentationSnapshot>();
                    return RemoteReplicaStoreError.MissingWholeBodyState;
                }
                headPosition = wholeBody.HeadPosition;
                headingRadians = wholeBody.HeadingRadians;
                radius = wholeBody.Diameter * 0.5f;
            }
            else
            {
                RemoteReplicaStoreError sampleError = Sample(
                    pair.Key,
                    estimatedServerTimeline,
                    out RemoteSnapshotSample transform);
                if (sampleError != RemoteReplicaStoreError.None)
                {
                    snapshots = Array.Empty<RemoteReplicaPresentationSnapshot>();
                    return sampleError;
                }
                headPosition = transform.Position;
                headingRadians = transform.AngleRadians;
            }

            built.Add(new RemoteReplicaPresentationSnapshot(
                pair.Key,
                pair.Value.EntityKind,
                radius,
                headPosition,
                headingRadians,
                wholeBody));
        }

        built.Sort(static (left, right) =>
        {
            int entityOrder = left.Key.EntityId.CompareTo(right.Key.EntityId);
            return entityOrder != 0
                ? entityOrder
                : left.Key.Generation.CompareTo(right.Key.Generation);
        });
        snapshots = built.AsReadOnly();
        return RemoteReplicaStoreError.None;
    }

    // 다른 RemoteEntity 의 움직임 보정
    // server snapshot 기반 interpolation / extrapolation
    internal RemoteReplicaStoreError Sample(
        ClientWorldEntityKey key,
        double estimatedServerTimeline,
        out RemoteSnapshotSample sample)
    {
        sample = default;
        if (!key.IsValid ||
            !double.IsFinite(estimatedServerTimeline) ||
            estimatedServerTimeline < 0.0)
        {
            return RemoteReplicaStoreError.InvalidArgument;
        }
        if (!replicas.TryGetValue(key, out RemoteReplica? replica))
        {
            return RemoteReplicaStoreError.MissingReplica;
        }

        // targetServerTick 은 현재 화면에 그릴 remote entity 의 위치 계산을 위한 tick. 실제 서버 시각보다 살짝 과거
        double targetServerTick = Math.Max(
            0.0,
            estimatedServerTimeline - interpolationDelayTicks);

        // snapshot history 에서 [snapshot A.tick] [targetServerTick] [snapshot B.tick] 인 값을 찾아 interpolation
        // maxExtrapolationTicks = (1, snapshotIntervalTicks * 2);
        RemoteSnapshotHistoryError historyError = replica.History.Sample(
            targetServerTick,
            tickRateHz,
            maxExtrapolationTicks,
            out sample);
        return historyError == RemoteSnapshotHistoryError.None
            ? RemoteReplicaStoreError.None
            : RemoteReplicaStoreError.InvalidArgument;
    }

    internal void Clear()
    {
        replicas.Clear();
    }
}
