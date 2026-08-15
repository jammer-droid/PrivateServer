using System;

using ControlledEntityStateV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using EntityStateBatchV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBatch;
using EntitySpawnV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntitySpawn;
using RoundResultV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.RoundResult;
using WorldReadyV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldReady;
using WorldOverviewSnapshotV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewSnapshot;
using WorldOverviewSnapshotV3 = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewSnapshot;

namespace PrivateServer.GameClient.Gameplay.Protocol.V1;

internal static class ServerGameplayPacketDecoder
{
    internal static GameplayProtocolError Decode(
        uint packetType,
        ReadOnlySpan<byte> payload,
        out ServerGameplayPacket? packet)
    {
        packet = null;

        switch (packetType)
        {
            case WorldReady.PacketTypeValue:
            {
                if (payload.Length < sizeof(ushort))
                {
                    return GameplayProtocolError.InvalidLength;
                }
                ushort payloadVersion = V1WireCodec.ReadU16(0, payload);
                if (payloadVersion == 1)
                {
                    GameplayProtocolError error = WorldReady.Decode(payload, out WorldReady? decoded);
                    packet = decoded;
                    return error;
                }
                if (payloadVersion == 2)
                {
                    GameplayProtocolError error = WorldReadyV2.Decode(payload, out WorldReadyV2? decoded);
                    packet = decoded;
                    return error;
                }
                return GameplayProtocolError.UnsupportedVersion;
            }
            case EntitySpawn.PacketTypeValue:
            {
                if (payload.Length < sizeof(ushort))
                {
                    return GameplayProtocolError.InvalidLength;
                }
                ushort payloadVersion = V1WireCodec.ReadU16(0, payload);
                if (payloadVersion == 1)
                {
                    GameplayProtocolError error = EntitySpawn.Decode(payload, out EntitySpawn? decoded);
                    packet = decoded;
                    return error;
                }
                if (payloadVersion == 2)
                {
                    GameplayProtocolError error = EntitySpawnV2.Decode(payload, out EntitySpawnV2? decoded);
                    packet = decoded;
                    return error;
                }
                return GameplayProtocolError.UnsupportedVersion;
            }
            case ControlledEntityState.PacketTypeValue:
            {
                if (payload.Length < sizeof(ushort))
                {
                    return GameplayProtocolError.InvalidLength;
                }
                ushort payloadVersion = V1WireCodec.ReadU16(0, payload);
                if (payloadVersion == 1)
                {
                    GameplayProtocolError error =
                        ControlledEntityState.Decode(payload, out ControlledEntityState? decoded);
                    packet = decoded;
                    return error;
                }
                if (payloadVersion == 2)
                {
                    GameplayProtocolError error =
                        ControlledEntityStateV2.Decode(payload, out ControlledEntityStateV2? decoded);
                    packet = decoded;
                    return error;
                }
                return GameplayProtocolError.UnsupportedVersion;
            }
            case EntityStateBatch.PacketTypeValue:
            {
                if (payload.Length < sizeof(ushort))
                {
                    return GameplayProtocolError.InvalidLength;
                }
                ushort payloadVersion = V1WireCodec.ReadU16(0, payload);
                if (payloadVersion == 1)
                {
                    GameplayProtocolError error = EntityStateBatch.Decode(payload, out EntityStateBatch? decoded);
                    packet = decoded;
                    return error;
                }
                if (payloadVersion == 2)
                {
                    GameplayProtocolError error = EntityStateBatchV2.Decode(payload, out EntityStateBatchV2? decoded);
                    packet = decoded;
                    return error;
                }
                return GameplayProtocolError.UnsupportedVersion;
            }
            case EntityRemove.PacketTypeValue:
            {
                GameplayProtocolError error = EntityRemove.Decode(payload, out EntityRemove? decoded);
                packet = decoded;
                return error;
            }
            case ScoreState.PacketTypeValue:
            {
                GameplayProtocolError error = ScoreState.Decode(payload, out ScoreState? decoded);
                packet = decoded;
                return error;
            }
            case RoundState.PacketTypeValue:
            {
                GameplayProtocolError error = RoundState.Decode(payload, out RoundState? decoded);
                packet = decoded;
                return error;
            }
            case WorldTimeSyncResponse.PacketTypeValue:
            {
                GameplayProtocolError error =
                    WorldTimeSyncResponse.Decode(payload, out WorldTimeSyncResponse? decoded);
                packet = decoded;
                return error;
            }
            case ControlledEntityRebind.PacketTypeValue:
            {
                GameplayProtocolError error =
                    ControlledEntityRebind.Decode(payload, out ControlledEntityRebind? decoded);
                packet = decoded;
                return error;
            }
            case WorldOverviewSnapshotV2.PacketTypeValue:
            {
                if (payload.Length < sizeof(ushort))
                {
                    return GameplayProtocolError.InvalidLength;
                }
                ushort payloadVersion = V1WireCodec.ReadU16(0, payload);
                if (payloadVersion == 2)
                {
                    GameplayProtocolError error =
                        WorldOverviewSnapshotV2.Decode(payload, out WorldOverviewSnapshotV2? decoded);
                    packet = decoded;
                    return error;
                }
                if (payloadVersion == 3)
                {
                    GameplayProtocolError error =
                        WorldOverviewSnapshotV3.Decode(payload, out WorldOverviewSnapshotV3? decoded);
                    packet = decoded;
                    return error;
                }
                return GameplayProtocolError.UnsupportedVersion;
            }
            case RoundResultV2.PacketTypeValue:
            {
                GameplayProtocolError error = RoundResultV2.Decode(payload, out RoundResultV2? decoded);
                packet = decoded;
                return error;
            }
            default:
                return GameplayProtocolError.InvalidEnum;
        }
    }
}
