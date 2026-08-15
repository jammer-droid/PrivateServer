using System;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using IClientGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.IClientGameplayPacket;
using V1WireCodec = PrivateServer.GameClient.Gameplay.Protocol.V1.V1WireCodec;

namespace PrivateServer.GameClient.Gameplay.Protocol.V2;

internal readonly record struct JoinWorldRequest(string DisplayName) : IClientGameplayPacket
{
    internal const uint PacketTypeValue = 0x0100;
    internal const int DisplayNameOffset = 4;
    internal const int MinimumPayloadBytes = DisplayNameOffset;
    internal const int MaximumPayloadBytes = DisplayNameOffset + PlayerDisplayNameRules.MaximumByteCount;

    uint IClientGameplayPacket.PacketType => PacketTypeValue;
    int IClientGameplayPacket.PayloadByteCount => CalculatePayloadBytes(DisplayName);

    internal static int CalculatePayloadBytes(string? displayName)
    {
        return displayName is not null && displayName.Length <= PlayerDisplayNameRules.MaximumByteCount
            ? DisplayNameOffset + displayName.Length
            : 0;
    }

    public GameplayProtocolError Encode(Span<byte> output)
    {
        int payloadBytes = CalculatePayloadBytes(DisplayName);
        if (payloadBytes == 0 || output.Length != payloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (!PlayerDisplayNameRules.IsValid(DisplayName))
        {
            return GameplayProtocolError.InvalidArgument;
        }

        V1WireCodec.WriteU16(2, 0, output);
        V1WireCodec.WriteU16((ushort)DisplayName.Length, 2, output);
        PlayerDisplayNameRules.Write(DisplayName, output[DisplayNameOffset..]);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out JoinWorldRequest value)
    {
        value = default;
        if (payload.Length < MinimumPayloadBytes || payload.Length > MaximumPayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }
        if (V1WireCodec.ReadU16(0, payload) != 2)
        {
            return GameplayProtocolError.UnsupportedVersion;
        }

        ushort displayNameByteCount = V1WireCodec.ReadU16(2, payload);
        if (displayNameByteCount != payload.Length - DisplayNameOffset)
        {
            return GameplayProtocolError.InvalidLength;
        }
        ReadOnlySpan<byte> displayNameBytes = payload[DisplayNameOffset..];
        if (!PlayerDisplayNameRules.IsValid(displayNameBytes))
        {
            return GameplayProtocolError.InvalidArgument;
        }

        value = new JoinWorldRequest(PlayerDisplayNameRules.Decode(displayNameBytes));
        return GameplayProtocolError.Success;
    }
}
