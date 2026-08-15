using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record EntityRemove(
    uint ServerTick,
    uint EntityId,
    uint Generation,
    EntityRemoveReason Reason) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0184;
    internal const int PayloadBytes = 16;

    internal override uint PacketType => PacketTypeValue;

    internal GameplayProtocolError Encode(Span<byte> output)
    {
        if (output.Length != PayloadBytes)
        {
            return GameplayProtocolError.InvalidLength;
        }

        GameplayProtocolError validationError = Validate(this);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }

        V1WireCodec.WriteVersion(output);
        V1WireCodec.WriteU32(ServerTick, 2, output);
        V1WireCodec.WriteU32(EntityId, 6, output);
        V1WireCodec.WriteU32(Generation, 10, output);
        V1WireCodec.WriteU16((ushort)Reason, 14, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out EntityRemove? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        EntityRemove decoded = new EntityRemove(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            (EntityRemoveReason)V1WireCodec.ReadU16(14, payload));
        GameplayProtocolError validationError = Validate(decoded);
        if (validationError != GameplayProtocolError.Success)
        {
            return validationError;
        }

        value = decoded;
        return GameplayProtocolError.Success;
    }

    private static GameplayProtocolError Validate(EntityRemove value)
    {
        if (!IsKnown(value.Reason))
        {
            return GameplayProtocolError.InvalidEnum;
        }
        if (value.EntityId == 0 || value.Generation == 0)
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        return GameplayProtocolError.Success;
    }

    private static bool IsKnown(EntityRemoveReason value)
    {
        return value == EntityRemoveReason.LeftAoi ||
            value == EntityRemoveReason.Destroyed ||
            value == EntityRemoveReason.Collected ||
            value == EntityRemoveReason.SessionClosed ||
            value == EntityRemoveReason.RoundReset;
    }
}
