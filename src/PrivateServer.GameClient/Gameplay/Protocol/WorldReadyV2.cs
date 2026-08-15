using System;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal sealed record WorldReady(
    uint PlayerId,
    uint ControlledEntityId,
    uint ControlledEntityGeneration,
    uint CurrentServerTick,
    uint TickRateHz,
    uint SnapshotIntervalTicks,
    uint CommandSlackTicks,
    float ArenaMinX,
    float ArenaMinY,
    float ArenaMaxX,
    float ArenaMaxY,
    uint ChannelId,
    string DisplayName) : ServerGameplayPacket
{
    internal const uint PacketTypeValue = 0x0180;
    internal const int ChannelIdOffset = 46;
    internal const int DisplayNameByteCountOffset = 50;
    internal const int DisplayNameOffset = 52;
    internal const int MinimumPayloadBytes = DisplayNameOffset;
    internal const int MaximumPayloadBytes = DisplayNameOffset + PlayerDisplayNameRules.MaximumByteCount;

    internal override uint PacketType => PacketTypeValue;

    internal static int CalculatePayloadBytes(string? displayName)
    {
        return displayName is not null && displayName.Length <= PlayerDisplayNameRules.MaximumByteCount
            ? DisplayNameOffset + displayName.Length
            : 0;
    }

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        int payloadBytes = CalculatePayloadBytes(DisplayName);
        if (payloadBytes == 0 || output.Length != payloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (!IsValidNumeric(this))
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        if (!PlayerDisplayNameRules.IsValid(DisplayName))
        {
            return GameplayProtocolError.InvalidArgument;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU32(PlayerId, 2, output);
        V1WireCodec.WriteU32(ControlledEntityId, 6, output);
        V1WireCodec.WriteU32(ControlledEntityGeneration, 10, output);
        V1WireCodec.WriteU32(CurrentServerTick, 14, output);
        V1WireCodec.WriteU32(TickRateHz, 18, output);
        V1WireCodec.WriteU32(SnapshotIntervalTicks, 22, output);
        V1WireCodec.WriteU32(CommandSlackTicks, 26, output);
        V1WireCodec.WriteF32(ArenaMinX, 30, output);
        V1WireCodec.WriteF32(ArenaMinY, 34, output);
        V1WireCodec.WriteF32(ArenaMaxX, 38, output);
        V1WireCodec.WriteF32(ArenaMaxY, 42, output);
        V1WireCodec.WriteU32(ChannelId, ChannelIdOffset, output);
        V1WireCodec.WriteU16((ushort)DisplayName.Length, DisplayNameByteCountOffset, output);
        PlayerDisplayNameRules.Write(DisplayName, output[DisplayNameOffset..]);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out WorldReady? value)
    {
        value = null;
        if (payload.Length < MinimumPayloadBytes || payload.Length > MaximumPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 2)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        ushort displayNameByteCount = V1WireCodec.ReadU16(DisplayNameByteCountOffset, payload);
        if (displayNameByteCount != payload.Length - DisplayNameOffset)
        {
            return GameplayProtocolError.InvalidLength;
        }
        ReadOnlySpan<byte> displayNameBytes = payload[DisplayNameOffset..];
        if (!PlayerDisplayNameRules.IsValid(displayNameBytes))
        {
            return GameplayProtocolError.InvalidArgument;
        }

        WorldReady decoded = new WorldReady(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            V1WireCodec.ReadU32(14, payload),
            V1WireCodec.ReadU32(18, payload),
            V1WireCodec.ReadU32(22, payload),
            V1WireCodec.ReadU32(26, payload),
            V1WireCodec.ReadF32(30, payload),
            V1WireCodec.ReadF32(34, payload),
            V1WireCodec.ReadF32(38, payload),
            V1WireCodec.ReadF32(42, payload),
            V1WireCodec.ReadU32(ChannelIdOffset, payload),
            PlayerDisplayNameRules.Decode(displayNameBytes));
        if (!IsValidNumeric(decoded))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static bool IsValidNumeric(WorldReady value)
    {
        return value.PlayerId != 0 &&
            value.ControlledEntityId != 0 &&
            value.ControlledEntityGeneration != 0 &&
            value.TickRateHz != 0 &&
            value.SnapshotIntervalTicks != 0 &&
            value.CommandSlackTicks != 0 &&
            value.ChannelId != 0 &&
            float.IsFinite(value.ArenaMinX) &&
            float.IsFinite(value.ArenaMinY) &&
            float.IsFinite(value.ArenaMaxX) &&
            float.IsFinite(value.ArenaMaxY) &&
            value.ArenaMinX < value.ArenaMaxX &&
            value.ArenaMinY < value.ArenaMaxY;
    }
}
