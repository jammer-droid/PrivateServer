using PrivateServer.GameClient.Gameplay.Protocol.V1;
using System;
using System.Collections.Generic;

namespace PrivateServer.GameClient.Gameplay.Remote;

internal enum WorldTimeSyncError
{
    None,
    ProbeAlreadyOutstanding,
    NoOutstandingProbe,
    NoValidSample,
    SequenceMismatch,
    InvalidTime,
    InvalidTickRate,
    SequenceOverflow,
    TickOverflow,
}

internal readonly record struct WorldTimeSyncSample(
    uint ProbeSequence,
    double ClientSendTimeSeconds,
    double ResponseReceivedTimeSeconds,
    double RoundTripTimeSeconds,
    uint ServerTick);

internal sealed class WorldTimeSyncTracker
{
    internal const int DefaultSampleCapacity = 8;

    private readonly int sampleCapacity;
    private readonly List<WorldTimeSyncSample> samples;
    private uint lastProbeSequence;
    private bool hasOutstandingProbe;
    private uint outstandingProbeSequence;
    private double outstandingProbeSendTimeSeconds;
    private double lastProbeSendTimeSeconds;
    private WorldTimeSyncSample? selectedSample;

    internal WorldTimeSyncTracker(int sampleCapacity = DefaultSampleCapacity)
    {
        if (sampleCapacity <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(sampleCapacity),
                "Time-sync sample capacity must be positive.");
        }

        this.sampleCapacity = sampleCapacity;
        samples = new List<WorldTimeSyncSample>(sampleCapacity);
    }

    internal bool HasOutstandingProbe => hasOutstandingProbe;
    internal bool HasValidSample => selectedSample.HasValue;
    internal int SampleCount => samples.Count;
    internal WorldTimeSyncSample? SelectedSample => selectedSample;

    internal WorldTimeSyncError BeginProbe(
        double nowSeconds,
        out WorldTimeSyncRequest request)
    {
        request = default;
        if (!IsValidTime(nowSeconds))
        {
            return WorldTimeSyncError.InvalidTime;
        }
        if (hasOutstandingProbe)
        {
            return WorldTimeSyncError.ProbeAlreadyOutstanding;
        }
        if (lastProbeSequence == uint.MaxValue)
        {
            return WorldTimeSyncError.SequenceOverflow;
        }

        ++lastProbeSequence;
        hasOutstandingProbe = true;
        outstandingProbeSequence = lastProbeSequence;
        outstandingProbeSendTimeSeconds = nowSeconds;
        lastProbeSendTimeSeconds = nowSeconds;
        request = new WorldTimeSyncRequest(outstandingProbeSequence);
        return WorldTimeSyncError.None;
    }

    internal WorldTimeSyncError AcceptResponse(
        WorldTimeSyncResponse response,
        double receivedAtSeconds,
        uint tickRateHz)
    {
        if (!hasOutstandingProbe)
        {
            return WorldTimeSyncError.NoOutstandingProbe;
        }
        if (response.ProbeSequence != outstandingProbeSequence)
        {
            return WorldTimeSyncError.SequenceMismatch;
        }
        if (!IsValidTime(receivedAtSeconds) ||
            receivedAtSeconds < outstandingProbeSendTimeSeconds)
        {
            return WorldTimeSyncError.InvalidTime;
        }
        if (tickRateHz == 0)
        {
            return WorldTimeSyncError.InvalidTickRate;
        }

        WorldTimeSyncSample sample = new WorldTimeSyncSample(
            response.ProbeSequence,
            outstandingProbeSendTimeSeconds,
            receivedAtSeconds,
            receivedAtSeconds - outstandingProbeSendTimeSeconds,
            response.ServerTick);

        hasOutstandingProbe = false;
        outstandingProbeSequence = 0;
        outstandingProbeSendTimeSeconds = 0.0;

        if (samples.Count == sampleCapacity)
        {
            samples.RemoveAt(0);
        }
        samples.Add(sample);
        SelectMinimumRoundTripSample();
        return WorldTimeSyncError.None;
    }

    internal bool IsPeriodicProbeDue(double nowSeconds, double intervalSeconds)
    {
        return !hasOutstandingProbe &&
            HasValidSample &&
            IsValidTime(nowSeconds) &&
            double.IsFinite(intervalSeconds) &&
            intervalSeconds > 0.0 &&
            nowSeconds - lastProbeSendTimeSeconds >= intervalSeconds;
    }

    internal WorldTimeSyncError EstimateServerTick(
        double nowSeconds,
        uint tickRateHz,
        out uint estimatedServerTick)
    {
        estimatedServerTick = 0;
        if (!selectedSample.HasValue)
        {
            return WorldTimeSyncError.NoValidSample;
        }
        if (!IsValidTime(nowSeconds) ||
            nowSeconds < selectedSample.Value.ResponseReceivedTimeSeconds)
        {
            return WorldTimeSyncError.InvalidTime;
        }
        if (tickRateHz == 0)
        {
            return WorldTimeSyncError.InvalidTickRate;
        }

        WorldTimeSyncSample sample = selectedSample.Value;
        double oneWayTicks = Math.Ceiling(sample.RoundTripTimeSeconds * tickRateHz / 2.0);
        double elapsedTicks = Math.Floor(
            (nowSeconds - sample.ResponseReceivedTimeSeconds) * tickRateHz);
        double estimate = sample.ServerTick + oneWayTicks + elapsedTicks;
        if (!double.IsFinite(estimate) || estimate > uint.MaxValue)
        {
            return WorldTimeSyncError.TickOverflow;
        }

        estimatedServerTick = checked((uint)estimate);
        return WorldTimeSyncError.None;
    }

    internal WorldTimeSyncError EstimateServerTimeline(
        double nowSeconds,
        uint tickRateHz,
        out double estimatedServerTimeline)
    {
        estimatedServerTimeline = 0.0;
        if (!selectedSample.HasValue)
        {
            return WorldTimeSyncError.NoValidSample;
        }
        if (!IsValidTime(nowSeconds) ||
            nowSeconds < selectedSample.Value.ResponseReceivedTimeSeconds)
        {
            return WorldTimeSyncError.InvalidTime;
        }
        if (tickRateHz == 0)
        {
            return WorldTimeSyncError.InvalidTickRate;
        }

        WorldTimeSyncSample sample = selectedSample.Value;
        double oneWayTicks = Math.Ceiling(sample.RoundTripTimeSeconds * tickRateHz / 2.0);
        double elapsedTicks =
            (nowSeconds - sample.ResponseReceivedTimeSeconds) * tickRateHz;
        double estimate = sample.ServerTick + oneWayTicks + elapsedTicks;
        if (!double.IsFinite(estimate) || estimate > uint.MaxValue)
        {
            return WorldTimeSyncError.TickOverflow;
        }

        estimatedServerTimeline = estimate;
        return WorldTimeSyncError.None;
    }

    internal void Reset()
    {
        samples.Clear();
        lastProbeSequence = 0;
        hasOutstandingProbe = false;
        outstandingProbeSequence = 0;
        outstandingProbeSendTimeSeconds = 0.0;
        lastProbeSendTimeSeconds = 0.0;
        selectedSample = null;
    }

    private static bool IsValidTime(double value)
    {
        return double.IsFinite(value) && value >= 0.0;
    }

    private void SelectMinimumRoundTripSample()
    {
        WorldTimeSyncSample selected = samples[0];
        for (int index = 1; index < samples.Count; ++index)
        {
            WorldTimeSyncSample candidate = samples[index];
            if (candidate.RoundTripTimeSeconds < selected.RoundTripTimeSeconds ||
                (candidate.RoundTripTimeSeconds == selected.RoundTripTimeSeconds &&
                 candidate.ResponseReceivedTimeSeconds > selected.ResponseReceivedTimeSeconds))
            {
                selected = candidate;
            }
        }

        selectedSample = selected;
    }
}
