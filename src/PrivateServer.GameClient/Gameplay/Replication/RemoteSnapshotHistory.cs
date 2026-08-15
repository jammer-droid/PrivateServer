using System;
using System.Collections.Generic;
using System.Numerics;

namespace PrivateServer.GameClient.Gameplay.Replication;

internal readonly record struct RemoteAuthoritativeSnapshot(
    uint ServerTick,
    Vector2 Position,
    Vector2 Velocity,
    float AngleRadians);

internal enum RemoteSnapshotSampleMode
{
    Held,
    Interpolated,
    Extrapolated,
    Frozen,
}

internal readonly record struct RemoteSnapshotSample(
    double TargetServerTick,
    Vector2 Position,
    Vector2 Velocity,
    float AngleRadians,
    RemoteSnapshotSampleMode Mode);

internal enum RemoteSnapshotHistoryError
{
    None,
    InvalidArgument,
    NonIncreasingServerTick,
    Empty,
}

internal sealed class RemoteSnapshotHistory
{
    internal const int DefaultCapacity = 2;

    private readonly int capacity;
    private readonly List<RemoteAuthoritativeSnapshot> snapshots;

    internal RemoteSnapshotHistory(int capacity = DefaultCapacity)
    {
        if (capacity < 2)
        {
            throw new ArgumentOutOfRangeException(
                nameof(capacity),
                "Remote snapshot history requires at least two slots.");
        }

        this.capacity = capacity;
        snapshots = new List<RemoteAuthoritativeSnapshot>(capacity);
    }

    internal int Count => snapshots.Count;
    internal uint? LatestServerTick =>
        snapshots.Count == 0 ? null : snapshots[^1].ServerTick;

    internal RemoteSnapshotHistoryError Reset(RemoteAuthoritativeSnapshot snapshot)
    {
        if (!IsValid(snapshot))
        {
            return RemoteSnapshotHistoryError.InvalidArgument;
        }

        snapshots.Clear();
        snapshots.Add(snapshot);
        return RemoteSnapshotHistoryError.None;
    }

    internal RemoteSnapshotHistoryError Add(RemoteAuthoritativeSnapshot snapshot)
    {
        if (!IsValid(snapshot))
        {
            return RemoteSnapshotHistoryError.InvalidArgument;
        }
        if (snapshots.Count > 0 &&
            snapshot.ServerTick <= snapshots[^1].ServerTick)
        {
            return RemoteSnapshotHistoryError.NonIncreasingServerTick;
        }

        if (snapshots.Count == capacity)
        {
            snapshots.RemoveAt(0);
        }
        snapshots.Add(snapshot);
        return RemoteSnapshotHistoryError.None;
    }

    internal RemoteSnapshotHistoryError ReplaceLatest(
        RemoteAuthoritativeSnapshot snapshot)
    {
        if (!IsValid(snapshot))
        {
            return RemoteSnapshotHistoryError.InvalidArgument;
        }
        if (snapshots.Count == 0)
        {
            return RemoteSnapshotHistoryError.Empty;
        }
        if (snapshot.ServerTick != snapshots[^1].ServerTick)
        {
            return RemoteSnapshotHistoryError.NonIncreasingServerTick;
        }

        snapshots[^1] = snapshot;
        return RemoteSnapshotHistoryError.None;
    }

    // Snapshot History 목록에서 targetServerTick 을 둘러싼 snapshot 을 찾아 interpolation/extrapolaion
    internal RemoteSnapshotHistoryError Sample(
        double targetServerTick,
        uint tickRateHz,
        uint maxExtrapolationTicks,
        out RemoteSnapshotSample sample)
    {
        sample = default;
        if (!double.IsFinite(targetServerTick) ||
            targetServerTick < 0.0 ||
            tickRateHz == 0 ||
            maxExtrapolationTicks == 0)
        {
            return RemoteSnapshotHistoryError.InvalidArgument;
        }
        if (snapshots.Count == 0)
        {
            return RemoteSnapshotHistoryError.Empty;
        }

        RemoteAuthoritativeSnapshot oldest = snapshots[0];
        if (targetServerTick <= oldest.ServerTick)
        {
            sample = MakeHeldSample(targetServerTick, oldest);
            return RemoteSnapshotHistoryError.None;
        }

        for (int index = 1; index < snapshots.Count; ++index)
        {
            RemoteAuthoritativeSnapshot newer = snapshots[index];
            if (targetServerTick > newer.ServerTick)
            {
                continue;
            }

            // [older] [targetServerTick] [newer] 사이를 interpolation
            RemoteAuthoritativeSnapshot older = snapshots[index - 1];
            double tickRange = newer.ServerTick - older.ServerTick;
            float amount = checked((float)((targetServerTick - older.ServerTick) / tickRange));

            sample = new RemoteSnapshotSample(
                targetServerTick,
                Vector2.Lerp(older.Position, newer.Position, amount),
                Vector2.Lerp(older.Velocity, newer.Velocity, amount),
                InterpolateAngleShortestArc(
                    older.AngleRadians,
                    newer.AngleRadians,
                    amount),
                RemoteSnapshotSampleMode.Interpolated);
            return RemoteSnapshotHistoryError.None;
        }

        // interpolation 이 불가능한 경우, 가장 최신의 snapshot 기반 extrapolation 진행
        //      - targetServerTick > latest.ServerTick 인 경우
        RemoteAuthoritativeSnapshot latest = snapshots[^1];
        double ticksAfterLatest = targetServerTick - latest.ServerTick; // extrapolation 범위
        double boundedTicks = Math.Min(ticksAfterLatest, maxExtrapolationTicks);
        float elapsedSeconds = checked((float)(boundedTicks / tickRateHz)); // extrapolation 비율
        Vector2 position = latest.Position + latest.Velocity * elapsedSeconds;
        bool frozen = ticksAfterLatest > maxExtrapolationTicks;
        sample = new RemoteSnapshotSample(
            targetServerTick,
            position,
            frozen ? Vector2.Zero : latest.Velocity,
            latest.AngleRadians,
            frozen
                ? RemoteSnapshotSampleMode.Frozen
                : RemoteSnapshotSampleMode.Extrapolated);
        return RemoteSnapshotHistoryError.None;
    }

    private static bool IsValid(RemoteAuthoritativeSnapshot snapshot)
    {
        return float.IsFinite(snapshot.Position.X) &&
            float.IsFinite(snapshot.Position.Y) &&
            float.IsFinite(snapshot.Velocity.X) &&
            float.IsFinite(snapshot.Velocity.Y) &&
            float.IsFinite(snapshot.AngleRadians);
    }

    private static RemoteSnapshotSample MakeHeldSample(
        double targetServerTick,
        RemoteAuthoritativeSnapshot snapshot)
    {
        return new RemoteSnapshotSample(
            targetServerTick,
            snapshot.Position,
            snapshot.Velocity,
            snapshot.AngleRadians,
            RemoteSnapshotSampleMode.Held);
    }

    // 두 각도 사이 회전할 때, 가장 짧은 방향으로 보간
    internal static float InterpolateAngleShortestArc(
        float from,
        float to,
        float amount)
    {
        // Matf.Tau = 2pi(=degree360), to, from 은 radian 이지만 편의상 degree 로 설명
        // 각도 차이 = to - from
        // ex) from = 170, to = -170
        // -170 - 170 = -340
        // -340 도를 360 도 기준으로 정규화
        // delta 는 20도
        float delta = MathF.IEEERemainder(to - from, MathF.Tau);

        // from 에서 delta * amount 회전하는 radian
        return MathF.IEEERemainder(from + delta * amount, MathF.Tau);
    }
}
