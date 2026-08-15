using System;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using ServerGameplayPacketDecoder = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacketDecoder;
using WorldOverviewLeaderboardEntry = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewLeaderboardEntry;
using WorldOverviewPlayer = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPlayer;
using WorldOverviewPoint = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPoint;
using WorldOverviewSnapshot = PrivateServer.GameClient.Gameplay.Protocol.V3.WorldOverviewSnapshot;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayProtocolV3Tests
{
    private const string WorldOverviewGolden =
        "0300040302011413121100000200000020C10000A0C0000020410000A0400000803F000000400000404001000100070000000900000002000000803F000000400000000000000040010007000000090000000700506C6179657237";

    [TestMethod]
    public void WorldOverviewSnapshotMatchesNativeGoldenBytesAndValidatesDisplayName()
    {
        WorldOverviewSnapshot expected = new WorldOverviewSnapshot(
            0x01020304, 0x11121314, 0, 2,
            -10.0f, -5.0f, 10.0f, 5.0f, 1.0f, 2.0f, 3.0f,
            new[]
            {
                new WorldOverviewPlayer(7, 9, new[]
                {
                    new WorldOverviewPoint(1.0f, 2.0f),
                    new WorldOverviewPoint(0.0f, 2.0f),
                }),
            },
            new[] { new WorldOverviewLeaderboardEntry(1, 7, 9, "Player7") });
        byte[] output = new byte[WorldOverviewSnapshot.CalculatePayloadBytes(expected)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(Convert.FromHexString(WorldOverviewGolden), output);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldOverviewSnapshot.Decode(output, out WorldOverviewSnapshot? decoded));
        Assert.IsNotNull(decoded);
        Assert.AreEqual("Player7", decoded.Leaderboard[0].DisplayName);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                WorldOverviewSnapshot.PacketTypeValue,
                output,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(WorldOverviewSnapshot));

        WorldOverviewSnapshot invalidName = new WorldOverviewSnapshot(
            expected.ServerTick, expected.OverviewId, expected.ChunkIndex, expected.ChunkCount,
            expected.MapMinX, expected.MapMinY, expected.MapMaxX, expected.MapMaxY,
            expected.ActiveAreaCenterX, expected.ActiveAreaCenterY, expected.ActiveAreaRadius,
            expected.Players,
            new[] { new WorldOverviewLeaderboardEntry(1, 7, 9, "Player 7") });
        byte[] invalidOutput = new byte[WorldOverviewSnapshot.CalculatePayloadBytes(invalidName)];
        Assert.AreEqual(GameplayProtocolError.InvalidArgument, invalidName.Encode(invalidOutput));

        byte[] truncatedName = (byte[])output.Clone();
        truncatedName[82] = 0x08;
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            WorldOverviewSnapshot.Decode(truncatedName, out WorldOverviewSnapshot? _));
        byte[] invalidCharacter = (byte[])output.Clone();
        invalidCharacter[84] = 0x2D;
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            WorldOverviewSnapshot.Decode(invalidCharacter, out WorldOverviewSnapshot? _));
    }
}
