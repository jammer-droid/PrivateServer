using System;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using GameplayProtocolError = PrivateServer.GameClient.Gameplay.Protocol.V1.GameplayProtocolError;
using BoostState = PrivateServer.GameClient.Gameplay.Protocol.V2.BoostState;
using ControlStateCommand = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlStateCommand;
using ControlledEntityBodySample = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityBodySample;
using ControlledEntityState = PrivateServer.GameClient.Gameplay.Protocol.V2.ControlledEntityState;
using EntityStateBatch = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBatch;
using EntityStateBodySample = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateBodySample;
using EntityStateRecord = PrivateServer.GameClient.Gameplay.Protocol.V2.EntityStateRecord;
using EntitySpawnV1 = PrivateServer.GameClient.Gameplay.Protocol.V1.EntitySpawn;
using EntitySpawnV2 = PrivateServer.GameClient.Gameplay.Protocol.V2.EntitySpawn;
using JoinWorldRequest = PrivateServer.GameClient.Gameplay.Protocol.V2.JoinWorldRequest;
using PlayerDisplayNameRules = PrivateServer.GameClient.Gameplay.Protocol.PlayerDisplayNameRules;
using RoundResult = PrivateServer.GameClient.Gameplay.Protocol.V2.RoundResult;
using ServerGameplayPacket = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacket;
using ServerGameplayPacketDecoder = PrivateServer.GameClient.Gameplay.Protocol.V1.ServerGameplayPacketDecoder;
using TurnState = PrivateServer.GameClient.Gameplay.Protocol.V2.TurnState;
using WorldReady = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldReady;
using WorldOverviewLeaderboardEntry = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewLeaderboardEntry;
using WorldOverviewPlayer = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPlayer;
using WorldOverviewPoint = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewPoint;
using WorldOverviewSnapshot = PrivateServer.GameClient.Gameplay.Protocol.V2.WorldOverviewSnapshot;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayProtocolV2Tests
{
    private const string ControlledEntityStateGolden =
        "02000403020114131211242322210000803F000000C0000040400000C03F02000000020002000000003F000000BF00008040000080C0";
    private const string EntityStateBatchGolden =
        "0200040302011413121100000200010007000000090000000000803F000000C0000040400000C03F02000000020002000000003F000000BF00008040000080C0";
    private const string WorldOverviewGolden =
        "0200040302011413121100000200000020C10000A0C0000020410000A0400000803F000000400000404001000100070000000900000002000000803F00000040000000000000004001000700000009000000";
    private const string RoundResultGolden =
        "02000403020114131211242322213433323102004443424154535251";

    [TestMethod]
    public void JoinWorldRequestMatchesNativeGoldenBytesAndValidatesDisplayName()
    {
        JoinWorldRequest expected = new JoinWorldRequest("Player7");
        byte[] golden = Convert.FromHexString("02000700506C6179657237");
        byte[] output = new byte[JoinWorldRequest.CalculatePayloadBytes(expected.DisplayName)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(golden, output);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            JoinWorldRequest.Decode(golden, out JoinWorldRequest decoded));
        Assert.AreEqual(expected, decoded);

        byte[] invalidLength = (byte[])golden.Clone();
        invalidLength[2] = 0x08;
        byte[] invalidSpace = (byte[])golden.Clone();
        invalidSpace[7] = 0x20;
        byte[] invalidNonAscii = (byte[])golden.Clone();
        invalidNonAscii[4] = 0xC3;
        byte[] invalidPunctuation = (byte[])golden.Clone();
        invalidPunctuation[4] = 0x2D;
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            JoinWorldRequest.Decode(invalidLength, out JoinWorldRequest _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            JoinWorldRequest.Decode(invalidSpace, out JoinWorldRequest _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            JoinWorldRequest.Decode(invalidNonAscii, out JoinWorldRequest _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            JoinWorldRequest.Decode(invalidPunctuation, out JoinWorldRequest _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            new JoinWorldRequest("Player 7").Encode(new byte[12]));
        Assert.AreEqual(0, JoinWorldRequest.CalculatePayloadBytes(new string('A', 49)));

        Assert.IsTrue(PlayerDisplayNameRules.IsValid(string.Empty));
        Assert.IsTrue(PlayerDisplayNameRules.IsValid("Abc123"));
        Assert.IsFalse(PlayerDisplayNameRules.IsValid("Player_7"));
        Assert.IsFalse(PlayerDisplayNameRules.IsValid("플레이어7"));
    }

    [TestMethod]
    public void WorldReadyMatchesNativeGoldenBytesAndDispatches()
    {
        WorldReady expected = new WorldReady(
            0x01020304,
            0x11121314,
            0x21222324,
            0x31323334,
            60,
            3,
            2,
            -10.0f,
            -5.0f,
            10.0f,
            5.0f,
            0x11223344,
            "Player7");
        byte[] golden = Convert.FromHexString(
            "0200040302011413121124232221343332313C0000000300000002000000000020C10000A0C0000020410000A040443322110700506C6179657237");
        byte[] output = new byte[WorldReady.CalculatePayloadBytes(expected.DisplayName)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(golden, output);
        Assert.AreEqual(GameplayProtocolError.Success, WorldReady.Decode(golden, out WorldReady? decoded));
        Assert.AreEqual(expected, decoded);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                WorldReady.PacketTypeValue,
                golden,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(WorldReady));

        byte[] invalidCharacter = (byte[])golden.Clone();
        invalidCharacter[WorldReady.DisplayNameOffset] = 0x2D;
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            WorldReady.Decode(invalidCharacter, out WorldReady? _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (expected with { ChannelId = 0 }).Encode(output));
    }

    [TestMethod]
    public void EntitySpawnMatchesNativeGoldenBytesAndDispatchesPlayerIdentity()
    {
        EntitySpawnV1 baseline = new EntitySpawnV1(
            0x01020304, 0x11121314, 0x21222324,
            PrivateServer.GameClient.Gameplay.Protocol.V1.EntityKind.Player,
            0x31323334,
            PrivateServer.GameClient.Gameplay.Protocol.V1.ShapeKind.Circle,
            1.5f, 2.0f, -1.0f, 2.0f, 0.5f, -0.5f, 3.0f);
        EntitySpawnV2 expected = new EntitySpawnV2(baseline, 0x41424344, "Player7");
        byte[] golden = Convert.FromHexString(
            "020004030201141312112423222101003433323101000000C03F00000040000080BF000000400000003F000000BF00004040444342410700506C6179657237");
        byte[] output = new byte[EntitySpawnV2.CalculatePayloadBytes(expected.DisplayName)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(golden, output);
        Assert.AreEqual(GameplayProtocolError.Success, EntitySpawnV2.Decode(golden, out EntitySpawnV2? decoded));
        Assert.AreEqual(expected, decoded);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                EntitySpawnV2.PacketTypeValue,
                golden,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(EntitySpawnV2));

        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            new EntitySpawnV2(baseline, 0, "Player7").Encode(output));
        byte[] invalidOutput = new byte[EntitySpawnV2.CalculatePayloadBytes("Player 7")];
        Assert.AreEqual(
            GameplayProtocolError.InvalidArgument,
            new EntitySpawnV2(baseline, 1, "Player 7").Encode(invalidOutput));
    }

    [TestMethod]
    public void WorldOverviewSnapshotMatchesNativeGoldenBytesAndDispatches()
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
            new[] { new WorldOverviewLeaderboardEntry(1, 7, 9) });
        byte[] output = new byte[WorldOverviewSnapshot.CalculatePayloadBytes(expected)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(Convert.FromHexString(WorldOverviewGolden), output);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            WorldOverviewSnapshot.Decode(output, out WorldOverviewSnapshot? decoded));
        Assert.IsNotNull(decoded);
        Assert.AreEqual(expected.OverviewId, decoded.OverviewId);
        Assert.AreEqual(expected.Players[0].BodySamples[1], decoded.Players[0].BodySamples[1]);
        Assert.AreEqual(expected.Leaderboard[0], decoded.Leaderboard[0]);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                WorldOverviewSnapshot.PacketTypeValue,
                output,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(WorldOverviewSnapshot));

        WorldOverviewSnapshot invalidChunk = new WorldOverviewSnapshot(
            expected.ServerTick, expected.OverviewId, 2, expected.ChunkCount,
            expected.MapMinX, expected.MapMinY, expected.MapMaxX, expected.MapMaxY,
            expected.ActiveAreaCenterX, expected.ActiveAreaCenterY, expected.ActiveAreaRadius,
            expected.Players, expected.Leaderboard);
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, invalidChunk.Encode(output));
        WorldOverviewSnapshot laterChunkWithLeaderboard = new WorldOverviewSnapshot(
            expected.ServerTick, expected.OverviewId, 1, expected.ChunkCount,
            expected.MapMinX, expected.MapMinY, expected.MapMaxX, expected.MapMaxY,
            expected.ActiveAreaCenterX, expected.ActiveAreaCenterY, expected.ActiveAreaRadius,
            expected.Players, expected.Leaderboard);
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, laterChunkWithLeaderboard.Encode(output));
    }

    [TestMethod]
    public void ControlStateCommandMatchesNativeGoldenBytes()
    {
        ControlStateCommand expected = new ControlStateCommand(
            0x01020304,
            0x0A0B0C0D,
            TurnState.Left,
            BoostState.On);
        byte[] golden = Convert.FromHexString("0200040302010D0C0B0A02000200");
        byte[] output = new byte[ControlStateCommand.PayloadBytes];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(golden, output);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ControlStateCommand.Decode(golden, out ControlStateCommand decoded));
        Assert.AreEqual(expected, decoded);

        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (expected with { InputSequence = 0 }).Encode(output));
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            (expected with { TurnState = TurnState.Invalid }).Encode(output));

        byte[] unsupportedVersion = (byte[])golden.Clone();
        unsupportedVersion[0] = 0x03;
        byte[] invalidTurn = (byte[])golden.Clone();
        invalidTurn[10] = 0x04;
        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            ControlStateCommand.Decode(unsupportedVersion, out ControlStateCommand _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            ControlStateCommand.Decode(invalidTurn, out ControlStateCommand _));
    }

    [TestMethod]
    public void ControlledEntityStateMatchesNativeGoldenBytes()
    {
        ControlledEntityState expected = MakeValidControlledEntityState();
        byte[] encoded = new byte[ControlledEntityState.CalculatePayloadBytes(expected.BodyTrailSamples.Count)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(encoded));
        CollectionAssert.AreEqual(Convert.FromHexString(ControlledEntityStateGolden), encoded);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ControlledEntityState.Decode(Convert.FromHexString(ControlledEntityStateGolden), out ControlledEntityState? decoded));
        Assert.IsNotNull(decoded);
        AssertControlledEntityStateEqual(expected, decoded);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                ControlledEntityState.PacketTypeValue,
                encoded,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(ControlledEntityState));
    }

    [TestMethod]
    public void ControlledEntityStateRejectsInvalidVersionLengthEnumAndNumericState()
    {
        ControlledEntityState valid = MakeValidControlledEntityState();
        byte[] validOutput = new byte[ControlledEntityState.CalculatePayloadBytes(valid.BodyTrailSamples.Count)];
        byte[] unsupportedVersion = Convert.FromHexString(ControlledEntityStateGolden);
        unsupportedVersion[0] = 0x01;
        byte[] invalidCount = Convert.FromHexString(ControlledEntityStateGolden);
        invalidCount[36] = 0x03;

        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            ControlledEntityState.Decode(unsupportedVersion, out ControlledEntityState? _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            ControlledEntityState.Decode(invalidCount, out ControlledEntityState? _));

        ControlledEntityState invalidEnum = MakeControlledEntityState(BoostState.Invalid, 1.5f, ValidBody());
        ControlledEntityState invalidDiameter = MakeControlledEntityState(BoostState.On, 0.0f, ValidBody());
        ControlledEntityState invalidBodySample = MakeControlledEntityState(
            BoostState.On,
            1.5f,
            new[] { new ControlledEntityBodySample(float.NaN, 0.0f) });
        ControlledEntityState emptyBody = MakeControlledEntityState(
            BoostState.On,
            1.5f,
            Array.Empty<ControlledEntityBodySample>());
        byte[] emptyBodyOutput = new byte[ControlledEntityState.HeaderBytes];

        Assert.AreEqual(GameplayProtocolError.InvalidEnum, invalidEnum.Encode(validOutput));
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, invalidDiameter.Encode(validOutput));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            invalidBodySample.Encode(new byte[ControlledEntityState.CalculatePayloadBytes(1)]));
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, emptyBody.Encode(emptyBodyOutput));
        Assert.AreEqual(
            8182,
            ControlledEntityState.CalculatePayloadBytes(ControlledEntityState.MaximumBodySampleCount));
        Assert.AreEqual(
            0,
            ControlledEntityState.CalculatePayloadBytes(ControlledEntityState.MaximumBodySampleCount + 1));
    }

    [TestMethod]
    public void ControlledEntityStateOwnsBodySamples()
    {
        ControlledEntityBodySample[] source = ValidBody();
        ControlledEntityState state = MakeControlledEntityState(BoostState.On, 1.5f, source);

        source[0] = new ControlledEntityBodySample(100.0f, 100.0f);

        Assert.AreEqual(new ControlledEntityBodySample(0.5f, -0.5f), state.BodyTrailSamples[0]);
    }

    [TestMethod]
    public void EntityStateBatchMatchesNativeGoldenBytesAndDispatchesV2()
    {
        EntityStateBodySample[] body =
        {
            new EntityStateBodySample(0.5f, -0.5f),
            new EntityStateBodySample(4.0f, -4.0f),
        };
        EntityStateBatch expected = new EntityStateBatch(
            0x01020304,
            0x11121314,
            0,
            2,
            new[]
            {
                new EntityStateRecord(7, 9, 1.0f, -2.0f, 3.0f, 1.5f, 2, BoostState.On, body),
            });
        byte[] output = new byte[EntityStateBatch.CalculatePayloadBytes(expected.Records)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(Convert.FromHexString(EntityStateBatchGolden), output);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            EntityStateBatch.Decode(output, out EntityStateBatch? decoded));
        Assert.IsNotNull(decoded);
        Assert.AreEqual(expected.SnapshotId, decoded.SnapshotId);
        Assert.AreEqual(expected.ChunkCount, decoded.ChunkCount);
        Assert.AreEqual(expected.Records[0].EntityId, decoded.Records[0].EntityId);
        Assert.AreEqual(expected.Records[0].BodyTrailSamples[1], decoded.Records[0].BodyTrailSamples[1]);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                EntityStateBatch.PacketTypeValue,
                output,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(EntityStateBatch));

        body[0] = new EntityStateBodySample(100.0f, 100.0f);
        Assert.AreEqual(new EntityStateBodySample(0.5f, -0.5f), expected.Records[0].BodyTrailSamples[0]);
    }

    [TestMethod]
    public void EntityStateBatchRejectsInvalidVersionLengthEnumAndNumericState()
    {
        byte[] unsupportedVersion = Convert.FromHexString(EntityStateBatchGolden);
        unsupportedVersion[0] = 0x03;
        byte[] invalidBodyCount = Convert.FromHexString(EntityStateBatchGolden);
        invalidBodyCount[EntityStateBatch.HeaderBytes + 30] = 0x03;

        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            EntityStateBatch.Decode(unsupportedVersion, out EntityStateBatch? _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            EntityStateBatch.Decode(invalidBodyCount, out EntityStateBatch? _));

        EntityStateRecord validRecord = new EntityStateRecord(
            7, 9, 1.0f, -2.0f, 3.0f, 1.5f, 2, BoostState.On,
            new[] { new EntityStateBodySample(0.5f, -0.5f) });
        EntityStateBatch invalidChunk = new EntityStateBatch(1, 1, 2, 2, new[] { validRecord });
        EntityStateRecord invalidEnumRecord = new EntityStateRecord(
            7, 9, 1.0f, -2.0f, 3.0f, 1.5f, 2, BoostState.Invalid,
            validRecord.BodyTrailSamples);
        EntityStateBatch invalidEnum = new EntityStateBatch(1, 1, 0, 1, new[] { invalidEnumRecord });
        EntityStateRecord emptyBodyRecord = new EntityStateRecord(
            7, 9, 1.0f, -2.0f, 3.0f, 1.5f, 2, BoostState.On,
            Array.Empty<EntityStateBodySample>());
        EntityStateBatch emptyBody = new EntityStateBatch(1, 1, 0, 1, new[] { emptyBodyRecord });

        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            invalidChunk.Encode(new byte[EntityStateBatch.CalculatePayloadBytes(invalidChunk.Records)]));
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            invalidEnum.Encode(new byte[EntityStateBatch.CalculatePayloadBytes(invalidEnum.Records)]));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            emptyBody.Encode(new byte[EntityStateBatch.CalculatePayloadBytes(emptyBody.Records)]));
    }

    [TestMethod]
    public void RoundResultMatchesNativeGoldenBytesAndDispatchesV2()
    {
        uint[] winnerPlayerIds = { 0x41424344, 0x51525354 };
        RoundResult expected = new RoundResult(
            0x01020304,
            0x11121314,
            0x21222324,
            0x31323334,
            winnerPlayerIds);
        byte[] output = new byte[RoundResult.CalculatePayloadBytes(expected.WinnerPlayerIds)];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(output));
        CollectionAssert.AreEqual(Convert.FromHexString(RoundResultGolden), output);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            RoundResult.Decode(output, out RoundResult? decoded));
        Assert.IsNotNull(decoded);
        Assert.AreEqual(expected.EndTick, decoded.EndTick);
        Assert.AreEqual(expected.RoundId, decoded.RoundId);
        Assert.AreEqual(expected.WinningGrowthPoint, decoded.WinningGrowthPoint);
        Assert.AreEqual(expected.RecipientFinalGrowthPoint, decoded.RecipientFinalGrowthPoint);
        CollectionAssert.AreEqual(winnerPlayerIds, (System.Collections.ICollection)decoded.WinnerPlayerIds);
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                RoundResult.PacketTypeValue,
                output,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(RoundResult));

        winnerPlayerIds[0] = 1;
        Assert.AreEqual(0x41424344u, expected.WinnerPlayerIds[0]);
    }

    [TestMethod]
    public void RoundResultSupportsNoWinnerAndRejectsMalformedWinnerLists()
    {
        RoundResult noWinner = new RoundResult(1, 1, 0, 0, Array.Empty<uint>());
        Assert.AreEqual(
            GameplayProtocolError.Success,
            noWinner.Encode(new byte[RoundResult.HeaderBytes]));

        RoundResult unsorted = new RoundResult(1, 1, 1, 0, new uint[] { 2, 1 });
        RoundResult zeroWinner = new RoundResult(1, 1, 1, 0, new uint[] { 0, 1 });
        RoundResult duplicateWinner = new RoundResult(1, 1, 1, 0, new uint[] { 1, 1 });
        RoundResult invalidNoWinner = new RoundResult(1, 1, 1, 0, Array.Empty<uint>());
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            unsorted.Encode(new byte[RoundResult.CalculatePayloadBytes(unsorted.WinnerPlayerIds)]));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            zeroWinner.Encode(new byte[RoundResult.CalculatePayloadBytes(zeroWinner.WinnerPlayerIds)]));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            duplicateWinner.Encode(new byte[RoundResult.CalculatePayloadBytes(duplicateWinner.WinnerPlayerIds)]));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            invalidNoWinner.Encode(new byte[RoundResult.HeaderBytes]));

        byte[] unsupportedVersion = Convert.FromHexString(RoundResultGolden);
        unsupportedVersion[0] = 0x01;
        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            RoundResult.Decode(unsupportedVersion, out RoundResult? _));
        byte[] invalidCount = Convert.FromHexString(RoundResultGolden);
        invalidCount[18] = 0x03;
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            RoundResult.Decode(invalidCount, out RoundResult? _));
        Assert.AreEqual(8184, RoundResult.CalculatePayloadBytes(new uint[RoundResult.MaximumWinnerCount]));
        Assert.AreEqual(0, RoundResult.CalculatePayloadBytes(new uint[RoundResult.MaximumWinnerCount + 1]));
    }

    private static ControlledEntityState MakeValidControlledEntityState()
    {
        return MakeControlledEntityState(BoostState.On, 1.5f, ValidBody());
    }

    private static ControlledEntityState MakeControlledEntityState(
        BoostState boostState,
        float diameter,
        ControlledEntityBodySample[] bodyTrailSamples)
    {
        return new ControlledEntityState(
            0x01020304,
            0x11121314,
            0x21222324,
            1.0f,
            -2.0f,
            3.0f,
            diameter,
            2,
            boostState,
            bodyTrailSamples);
    }

    private static ControlledEntityBodySample[] ValidBody()
    {
        return new[]
        {
            new ControlledEntityBodySample(0.5f, -0.5f),
            new ControlledEntityBodySample(4.0f, -4.0f),
        };
    }

    private static void AssertControlledEntityStateEqual(
        ControlledEntityState expected,
        ControlledEntityState actual)
    {
        Assert.AreEqual(expected.PacketType, actual.PacketType);
        Assert.AreEqual(expected.ServerTick, actual.ServerTick);
        Assert.AreEqual(expected.ControlledEntityGeneration, actual.ControlledEntityGeneration);
        Assert.AreEqual(expected.LastProcessedControlSequence, actual.LastProcessedControlSequence);
        Assert.AreEqual(expected.HeadPositionX, actual.HeadPositionX);
        Assert.AreEqual(expected.HeadPositionY, actual.HeadPositionY);
        Assert.AreEqual(expected.HeadingRadians, actual.HeadingRadians);
        Assert.AreEqual(expected.Diameter, actual.Diameter);
        Assert.AreEqual(expected.GrowthPoint, actual.GrowthPoint);
        Assert.AreEqual(expected.BoostState, actual.BoostState);
        Assert.AreEqual(expected.BodyTrailSamples.Count, actual.BodyTrailSamples.Count);
        for (int index = 0; index < expected.BodyTrailSamples.Count; ++index)
        {
            Assert.AreEqual(expected.BodyTrailSamples[index], actual.BodyTrailSamples[index]);
        }
    }
}
