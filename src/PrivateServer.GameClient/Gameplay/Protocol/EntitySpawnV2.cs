using System;

using EntitySpawnV1 = PrivateServer.GameClient.Gameplay.Protocol.V1.EntitySpawn;
using EntityKind = PrivateServer.GameClient.Gameplay.Protocol.V1.EntityKind;
using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal sealed record EntitySpawn(
    EntitySpawnV1 Baseline,
    uint PlayerId,
    string DisplayName) : ServerGameplayPacket
{
    internal const uint PacketTypeValue = EntitySpawnV1.PacketTypeValue;
    internal const int PlayerIdOffset = EntitySpawnV1.PayloadBytes;
    internal const int DisplayNameByteCountOffset = PlayerIdOffset + sizeof(uint);
    internal const int DisplayNameOffset = DisplayNameByteCountOffset + sizeof(ushort);
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
        GameplayProtocolError validationError = Validate(this);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }
        GameplayProtocolError baselineError = Baseline.Encode(output[..EntitySpawnV1.PayloadBytes]);
        if (baselineError != GameplayProtocolError.Success)
        {
            return baselineError;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU32(PlayerId, PlayerIdOffset, output);
        V1WireCodec.WriteU16(checked((ushort)DisplayName.Length), DisplayNameByteCountOffset, output);
        PlayerDisplayNameRules.Write(DisplayName, output[DisplayNameOffset..]);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(ReadOnlySpan<byte> payload, out EntitySpawn? value)
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
        int displayNameByteCount = V1WireCodec.ReadU16(DisplayNameByteCountOffset, payload);
        if (payload.Length != DisplayNameOffset + displayNameByteCount)
        {
            return GameplayProtocolError.InvalidLength;
        }

        Span<byte> baselinePayload = stackalloc byte[EntitySpawnV1.PayloadBytes];
        payload[..EntitySpawnV1.PayloadBytes].CopyTo(baselinePayload);
        V1WireCodec.WriteU16(1, 0, baselinePayload);
        GameplayProtocolError baselineError = EntitySpawnV1.Decode(baselinePayload, out EntitySpawnV1? baseline);
        if (baselineError != GameplayProtocolError.Success || baseline is null)
        {
            return baselineError;
        }
        ReadOnlySpan<byte> displayNameBytes = payload[DisplayNameOffset..];
        if (!PlayerDisplayNameRules.IsValid(displayNameBytes))
        {
            return GameplayProtocolError.InvalidArgument;
        }
        EntitySpawn decoded = new EntitySpawn(
            baseline,
            V1WireCodec.ReadU32(PlayerIdOffset, payload),
            PlayerDisplayNameRules.Decode(displayNameBytes));
        GameplayProtocolError validationError = Validate(decoded);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }
        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static GameplayProtocolError Validate(EntitySpawn value)
    {
        if (!PlayerDisplayNameRules.IsValid(value.DisplayName))
        {
            return GameplayProtocolError.InvalidArgument;
        }
        bool isPlayer = value.Baseline.EntityKind == EntityKind.Player;
        if ((isPlayer && value.PlayerId == 0) ||
            (!isPlayer && (value.PlayerId != 0 || value.DisplayName.Length != 0)))
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        return GameplayProtocolError.Success;
    }
}
