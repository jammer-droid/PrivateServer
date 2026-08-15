using System;
using System.Collections.Generic;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal enum BoostState : ushort
{
    Invalid = 0,
    Off = 1,
    On = 2,
}

internal readonly record struct ControlledEntityBodySample(float PositionX, float PositionY);

internal sealed record ControlledEntityState : ServerGameplayPacket
{
    internal const uint PacketTypeValue = 0x0182;
    internal const int HeaderBytes = 38;
    internal const int BodySampleBytes = 8;
    internal const int MaximumPayloadBytes = 8186;
    internal const int MaximumBodySampleCount = (MaximumPayloadBytes - HeaderBytes) / BodySampleBytes;

    internal ControlledEntityState(
        uint serverTick,
        uint controlledEntityGeneration,
        uint lastProcessedControlSequence,
        float headPositionX,
        float headPositionY,
        float headingRadians,
        float diameter,
        uint growthPoint,
        BoostState boostState,
        IReadOnlyList<ControlledEntityBodySample> bodyTrailSamples)
    {
        ArgumentNullException.ThrowIfNull(bodyTrailSamples);

        ControlledEntityBodySample[] ownedSamples = new ControlledEntityBodySample[bodyTrailSamples.Count];
        for (int index = 0; index < bodyTrailSamples.Count; ++index)
        {
            ownedSamples[index] = bodyTrailSamples[index];
        }

        ServerTick = serverTick;
        ControlledEntityGeneration = controlledEntityGeneration;
        LastProcessedControlSequence = lastProcessedControlSequence;
        HeadPositionX = headPositionX;
        HeadPositionY = headPositionY;
        HeadingRadians = headingRadians;
        Diameter = diameter;
        GrowthPoint = growthPoint;
        BoostState = boostState;
        BodyTrailSamples = Array.AsReadOnly(ownedSamples);
    }

    internal override uint PacketType => PacketTypeValue;
    internal uint ServerTick { get; }
    internal uint ControlledEntityGeneration { get; }
    internal uint LastProcessedControlSequence { get; }
    internal float HeadPositionX { get; }
    internal float HeadPositionY { get; }
    internal float HeadingRadians { get; }
    internal float Diameter { get; }
    internal uint GrowthPoint { get; }
    internal BoostState BoostState { get; }
    internal IReadOnlyList<ControlledEntityBodySample> BodyTrailSamples { get; }

    internal static int CalculatePayloadBytes(int bodySampleCount)
    {
        if (bodySampleCount < 0 || bodySampleCount > MaximumBodySampleCount)
        {
            return 0;
        }

        return HeaderBytes + (bodySampleCount * BodySampleBytes);
    }

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        int bodySampleCount = BodyTrailSamples.Count;
        if (bodySampleCount > MaximumBodySampleCount || output.Length != CalculatePayloadBytes(bodySampleCount))
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (BoostState != PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState.Off &&
            BoostState != PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState.On)
        {
            return GameplayProtocolError.InvalidEnum;
        }
        if (!IsValid(this))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(ControlledEntityGeneration, 6, output);
        V1WireCodec.WriteU32(LastProcessedControlSequence, 10, output);
        V1WireCodec.WriteF32(HeadPositionX, 14, output);
        V1WireCodec.WriteF32(HeadPositionY, 18, output);
        V1WireCodec.WriteF32(HeadingRadians, 22, output);
        V1WireCodec.WriteF32(Diameter, 26, output);
        V1WireCodec.WriteU32(GrowthPoint, 30, output);
        V1WireCodec.WriteU16((ushort)BoostState, 34, output);
        V1WireCodec.WriteU16(checked((ushort)bodySampleCount), 36, output);
        for (int index = 0; index < bodySampleCount; ++index)
        {
            WriteBodySample(BodyTrailSamples[index], HeaderBytes + (index * BodySampleBytes), output);
        }

        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(ReadOnlySpan<byte> payload, out ControlledEntityState? value)
    {
        value = null;
        if (payload.Length < HeaderBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 2)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        int bodySampleCount = V1WireCodec.ReadU16(36, payload);
        if (bodySampleCount > MaximumBodySampleCount || payload.Length != CalculatePayloadBytes(bodySampleCount))
        {
            return GameplayProtocolError.InvalidLength;
        }

        BoostState boostState = (BoostState)V1WireCodec.ReadU16(34, payload);
        if (boostState != BoostState.Off && boostState != BoostState.On)
        {
            return GameplayProtocolError.InvalidEnum;
        }

        ControlledEntityBodySample[] bodyTrailSamples = new ControlledEntityBodySample[bodySampleCount];
        for (int index = 0; index < bodySampleCount; ++index)
        {
            bodyTrailSamples[index] = ReadBodySample(HeaderBytes + (index * BodySampleBytes), payload);
        }

        ControlledEntityState decoded = new ControlledEntityState(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            V1WireCodec.ReadF32(14, payload),
            V1WireCodec.ReadF32(18, payload),
            V1WireCodec.ReadF32(22, payload),
            V1WireCodec.ReadF32(26, payload),
            V1WireCodec.ReadU32(30, payload),
            boostState,
            bodyTrailSamples);
        if (!IsValid(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValid(ControlledEntityState value)
    {
        if (value.ControlledEntityGeneration == 0 ||
            !float.IsFinite(value.HeadPositionX) ||
            !float.IsFinite(value.HeadPositionY) ||
            !float.IsFinite(value.HeadingRadians) ||
            !float.IsFinite(value.Diameter) ||
            value.Diameter <= 0.0f ||
            value.BodyTrailSamples.Count == 0)
        {
            return false;
        }

        for (int index = 0; index < value.BodyTrailSamples.Count; ++index)
        {
            ControlledEntityBodySample sample = value.BodyTrailSamples[index];
            if (!float.IsFinite(sample.PositionX) || !float.IsFinite(sample.PositionY))
            {
                return false;
            }
        }

        return true;
    }

    private static void WriteBodySample(ControlledEntityBodySample sample, int offset, Span<byte> output)
    {
        V1WireCodec.WriteF32(sample.PositionX, offset, output);
        V1WireCodec.WriteF32(sample.PositionY, offset + sizeof(float), output);
    }

    private static ControlledEntityBodySample ReadBodySample(int offset, ReadOnlySpan<byte> payload)
    {
        return new ControlledEntityBodySample(
            V1WireCodec.ReadF32(offset, payload),
            V1WireCodec.ReadF32(offset + sizeof(float), payload));
    }
}
