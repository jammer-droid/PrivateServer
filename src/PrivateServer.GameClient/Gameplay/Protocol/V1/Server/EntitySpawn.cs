using System;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal sealed record EntitySpawn(
    uint ServerTick,
    uint EntityId,
    uint Generation,
    EntityKind EntityKind,
    uint ArchetypeId,
    ShapeKind PrimaryShapeKind,
    float PrimaryCircleRadius,
    float MaxMoveSpeed,
    float PositionX,
    float PositionY,
    float VelocityX,
    float VelocityY,
    float AngleRadians) : ServerGameplayPacketV1
{
    internal const uint PacketTypeValue = 0x0181;
    internal const int PayloadBytes = 50;

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
        V1WireCodec.WriteU16((ushort)EntityKind, 14, output);
        V1WireCodec.WriteU32(ArchetypeId, 16, output);
        V1WireCodec.WriteU16((ushort)PrimaryShapeKind, 20, output);
        V1WireCodec.WriteF32(PrimaryCircleRadius, 22, output);
        V1WireCodec.WriteF32(MaxMoveSpeed, 26, output);
        V1WireCodec.WriteF32(PositionX, 30, output);
        V1WireCodec.WriteF32(PositionY, 34, output);
        V1WireCodec.WriteF32(VelocityX, 38, output);
        V1WireCodec.WriteF32(VelocityY, 42, output);
        V1WireCodec.WriteF32(AngleRadians, 46, output);
        return GameplayProtocolError.Success;
    }

    internal static GameplayProtocolError Decode(
        ReadOnlySpan<byte> payload,
        out EntitySpawn? value)
    {
        value = null;
        GameplayProtocolError headerError = V1WireCodec.ValidateFixedPayload(payload, PayloadBytes);
        if (headerError != GameplayProtocolError.Success)
        {
            return headerError;
        }

        EntitySpawn decoded = new EntitySpawn(
            V1WireCodec.ReadU32(2, payload),
            V1WireCodec.ReadU32(6, payload),
            V1WireCodec.ReadU32(10, payload),
            (EntityKind)V1WireCodec.ReadU16(14, payload),
            V1WireCodec.ReadU32(16, payload),
            (ShapeKind)V1WireCodec.ReadU16(20, payload),
            V1WireCodec.ReadF32(22, payload),
            V1WireCodec.ReadF32(26, payload),
            V1WireCodec.ReadF32(30, payload),
            V1WireCodec.ReadF32(34, payload),
            V1WireCodec.ReadF32(38, payload),
            V1WireCodec.ReadF32(42, payload),
            V1WireCodec.ReadF32(46, payload));
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
        if (!IsKnown(value.EntityKind) || value.PrimaryShapeKind != ShapeKind.Circle)
        {
            return GameplayProtocolError.InvalidEnum;
        }
        if (value.EntityId == 0 ||
            value.Generation == 0 ||
            value.ArchetypeId == 0 ||
            !float.IsFinite(value.PrimaryCircleRadius) ||
            !float.IsFinite(value.MaxMoveSpeed) ||
            !float.IsFinite(value.PositionX) ||
            !float.IsFinite(value.PositionY) ||
            !float.IsFinite(value.VelocityX) ||
            !float.IsFinite(value.VelocityY) ||
            !float.IsFinite(value.AngleRadians) ||
            value.PrimaryCircleRadius <= 0.0f)
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        if (value.EntityKind == EntityKind.Player && value.MaxMoveSpeed <= 0.0f)
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        if (value.EntityKind != EntityKind.Player && value.MaxMoveSpeed != 0.0f)
        {
            return GameplayProtocolError.InvalidNumeric;
        }
        if (value.EntityKind == EntityKind.StaticObstacle &&
            (value.VelocityX != 0.0f || value.VelocityY != 0.0f))
        {
            return GameplayProtocolError.InvalidNumeric;
        }

        return GameplayProtocolError.Success;
    }

    private static bool IsKnown(EntityKind value)
    {
        return value == EntityKind.Player ||
            value == EntityKind.Resource ||
            value == EntityKind.StaticObstacle;
    }
}
