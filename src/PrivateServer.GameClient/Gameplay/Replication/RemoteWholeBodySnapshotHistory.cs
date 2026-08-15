using System;
using System.Collections.Generic;
using System.Numerics;

using BoostStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using EntityStateBodySampleV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBodySample;

namespace PrivateServer.GameClient.Gameplay.Replication;

internal enum RemoteWholeBodySampleMode
{
    Held,
    Interpolated,
}

internal sealed record RemoteWholeBodyPresentationSample
{
    internal RemoteWholeBodyPresentationSample(
        double targetServerTick,
        Vector2 headPosition,
        float headingRadians,
        float diameter,
        uint growthPoint,
        BoostStateV2 boostState,
        IReadOnlyList<Vector2> bodyTrail,
        RemoteWholeBodySampleMode mode)
    {
        TargetServerTick = targetServerTick;
        HeadPosition = headPosition;
        HeadingRadians = headingRadians;
        Diameter = diameter;
        GrowthPoint = growthPoint;
        BoostState = boostState;
        BodyTrail = bodyTrail;
        Mode = mode;
    }

    internal double TargetServerTick { get; }
    internal Vector2 HeadPosition { get; }
    internal float HeadingRadians { get; }
    internal float Diameter { get; }
    internal uint GrowthPoint { get; }
    internal BoostStateV2 BoostState { get; }
    internal IReadOnlyList<Vector2> BodyTrail { get; }
    internal RemoteWholeBodySampleMode Mode { get; }
}

internal sealed class RemoteWholeBodySnapshotHistory
{
    private const float MinimumSegmentLength = 0.000001f;
    private readonly List<PreparedSnapshot> snapshots = new List<PreparedSnapshot>(2);

    internal RemoteWholeBodySnapshot? LatestSnapshot =>
        snapshots.Count == 0 ? null : snapshots[^1].Snapshot;

    internal void Clear()
    {
        snapshots.Clear();
    }

    internal bool CanApply(uint serverTick, uint initialServerTick)
    {
        uint latestServerTick = LatestSnapshot.HasValue
            ? LatestSnapshot.Value.ServerTick
            : initialServerTick;
        return serverTick >= latestServerTick;
    }

    internal void AddOrReplace(RemoteWholeBodySnapshot snapshot)
    {
        PreparedSnapshot prepared = new PreparedSnapshot(snapshot);
        if (snapshots.Count > 0 && snapshots[^1].Snapshot.ServerTick == snapshot.ServerTick)
        {
            snapshots[^1] = prepared;
            return;
        }
        if (snapshots.Count == 2)
        {
            snapshots.RemoveAt(0);
        }
        snapshots.Add(prepared);
    }

    internal bool TrySample(
        double targetServerTick,
        out RemoteWholeBodyPresentationSample? sample)
    {
        sample = null;
        if (!double.IsFinite(targetServerTick) || targetServerTick < 0.0 || snapshots.Count == 0)
        {
            return false;
        }

        PreparedSnapshot oldest = snapshots[0];
        if (snapshots.Count == 1 || targetServerTick <= oldest.Snapshot.ServerTick)
        {
            sample = MakeHeld(targetServerTick, oldest);
            return true;
        }

        PreparedSnapshot latest = snapshots[^1];
        if (targetServerTick >= latest.Snapshot.ServerTick)
        {
            sample = MakeHeld(targetServerTick, latest);
            return true;
        }

        double tickRange = latest.Snapshot.ServerTick - oldest.Snapshot.ServerTick;
        float amount = checked((float)((targetServerTick - oldest.Snapshot.ServerTick) / tickRange));
        int bodySampleCount = Math.Max(oldest.BodyTrail.Count, latest.BodyTrail.Count);
        Vector2[] bodyTrail = new Vector2[bodySampleCount];
        int oldestSegmentIndex = 0;
        int latestSegmentIndex = 0;

        // 두 snapshot의 body sample 수와 sample 간격은 서로 다를 수 있다.
        // 같은 배열 index끼리 바로 보간하면 이전 snapshot의 tail이 다음 snapshot의
        // body 중간점과 대응하는 것처럼 서로 다른 부위가 섞일 수 있다.
        //
        // 따라서 각 trail을 head-to-tail 누적 거리 기준의 0~1 구간으로 보고,
        // 두 trail 중 더 많은 sample 수만큼 동일한 normalizedDistance를 조회한다.
        // 예를 들어 2개/3개 sample이면 두 trail 모두 0%, 50%, 100% 지점을 구해
        // head 쪽은 head 쪽끼리, tail 쪽은 tail 쪽끼리 대응시킨다.
        //
        // normalizedDistance는 "body의 어느 위치인가"를 뜻하고,
        // amount는 "두 server tick 사이의 어느 시점인가"를 뜻한다.
        // 먼저 같은 body 위치의 old/new point를 찾은 뒤 amount로 시간 보간한다.
        for (int index = 0; index < bodySampleCount; ++index)
        {
            float normalizedDistance = bodySampleCount == 1
                ? 0.0f
                : (float)index / (bodySampleCount - 1);
            Vector2 oldestPoint = oldest.SampleBody(normalizedDistance, ref oldestSegmentIndex);
            Vector2 latestPoint = latest.SampleBody(normalizedDistance, ref latestSegmentIndex);
            bodyTrail[index] = Vector2.Lerp(oldestPoint, latestPoint, amount);
        }

        sample = new RemoteWholeBodyPresentationSample(
            targetServerTick,
            Vector2.Lerp(oldest.HeadPosition, latest.HeadPosition, amount),
            RemoteSnapshotHistory.InterpolateAngleShortestArc(
                oldest.Snapshot.State.HeadingRadians,
                latest.Snapshot.State.HeadingRadians,
                amount),
            float.Lerp(oldest.Snapshot.State.Diameter, latest.Snapshot.State.Diameter, amount),
            oldest.Snapshot.State.GrowthPoint,
            oldest.Snapshot.State.BoostState,
            Array.AsReadOnly(bodyTrail),
            RemoteWholeBodySampleMode.Interpolated);
        return true;
    }

    private static RemoteWholeBodyPresentationSample MakeHeld(
        double targetServerTick,
        PreparedSnapshot snapshot)
    {
        return new RemoteWholeBodyPresentationSample(
            targetServerTick,
            snapshot.HeadPosition,
            snapshot.Snapshot.State.HeadingRadians,
            snapshot.Snapshot.State.Diameter,
            snapshot.Snapshot.State.GrowthPoint,
            snapshot.Snapshot.State.BoostState,
            snapshot.BodyTrail,
            RemoteWholeBodySampleMode.Held);
    }

    private sealed class PreparedSnapshot
    {
        private readonly float[] cumulativeDistances;

        internal PreparedSnapshot(RemoteWholeBodySnapshot snapshot)
        {
            Snapshot = snapshot;
            HeadPosition = new Vector2(
                snapshot.State.HeadPositionX,
                snapshot.State.HeadPositionY);
            Vector2[] bodyTrail = new Vector2[snapshot.State.BodyTrailSamples.Count];
            cumulativeDistances = new float[bodyTrail.Length];

            // frame마다 trail 길이를 다시 계산하지 않도록 snapshot을 받을 때
            // 각 sample까지의 head-to-tail 누적 거리를 한 번 준비한다.
            for (int index = 0; index < bodyTrail.Length; ++index)
            {
                EntityStateBodySampleV2 source = snapshot.State.BodyTrailSamples[index];
                bodyTrail[index] = new Vector2(source.PositionX, source.PositionY);
                if (index > 0)
                {
                    cumulativeDistances[index] = cumulativeDistances[index - 1] +
                        Vector2.Distance(bodyTrail[index - 1], bodyTrail[index]);
                }
            }
            BodyTrail = Array.AsReadOnly(bodyTrail);
        }

        internal RemoteWholeBodySnapshot Snapshot { get; }
        internal Vector2 HeadPosition { get; }
        internal IReadOnlyList<Vector2> BodyTrail { get; }

        internal Vector2 SampleBody(float normalizedDistance, ref int segmentIndex)
        {
            if (BodyTrail.Count == 1 || cumulativeDistances[^1] <= MinimumSegmentLength)
            {
                return BodyTrail[0];
            }

            float targetDistance = cumulativeDistances[^1] * normalizedDistance;

            // segmentIndex는 targetDistance를 포함하는 원본 start-end 선분의
            // start point를 가리키고, segmentIndex + 1은 같은 선분의 end point다.
            // 예를 들어 누적 거리 [0, 4, 10]에서 target이 1, 2, 3이면 모두
            // start=0/end=4 선분 안에 있으므로 segmentIndex는 0으로 유지된다.
            // 이때 index를 바꾸는 대신 아래 amount가 각각 0.25, 0.5, 0.75로
            // 달라져 같은 두 원본 sample 사이의 여러 위치를 계산한다.
            // target이 5처럼 현재 end 거리 4를 넘어갈 때만 segmentIndex를 1로
            // 증가시켜 다음 start=4/end=10 선분으로 이동한다.
            //
            // 호출자가 normalizedDistance를 오름차순으로 요청하므로 이전에 찾은
            // segment부터 이어서 탐색할 수 있다. 한 frame의 전체 resampling 비용은
            // sample마다 처음부터 찾는 O(n^2)이 아니라 O(n)으로 유지된다.
            while (segmentIndex + 1 < cumulativeDistances.Length &&
                   cumulativeDistances[segmentIndex + 1] < targetDistance)
            {
                ++segmentIndex;
            }
            if (segmentIndex + 1 == cumulativeDistances.Length)
            {
                return BodyTrail[^1];
            }

            float segmentStart = cumulativeDistances[segmentIndex];
            float segmentLength = cumulativeDistances[segmentIndex + 1] - segmentStart;
            if (segmentLength <= MinimumSegmentLength)
            {
                return BodyTrail[segmentIndex + 1];
            }
            float amount = (targetDistance - segmentStart) / segmentLength;

            // segmentIndex는 "어느 start-end 선분인가", amount는 "그 선분 안의
            // 어느 위치인가"를 나타낸다. index가 그대로여도 amount가 달라지므로
            // 동일한 두 원본 sample 사이에 필요한 만큼 출력 point를 만들 수 있다.
            return Vector2.Lerp(BodyTrail[segmentIndex], BodyTrail[segmentIndex + 1], amount);
        }
    }
}
