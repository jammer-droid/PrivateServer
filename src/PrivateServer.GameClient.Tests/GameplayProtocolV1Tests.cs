using System;

using Microsoft.VisualStudio.TestTools.UnitTesting;

using PrivateServer.GameClient.Gameplay.Protocol.V1;

namespace PrivateServer.GameClient.Tests;

[TestClass]
public sealed class GameplayProtocolV1Tests
{
    private delegate GameplayProtocolError DecodeValuePacket<T>(
        ReadOnlySpan<byte> payload,
        out T value)
        where T : struct;

    private delegate GameplayProtocolError DecodeServerPacket<T>(
        ReadOnlySpan<byte> payload,
        out T? value)
        where T : ServerGameplayPacketV1;

    private delegate GameplayProtocolError EncodeServerPacket<T>(
        T packet,
        Span<byte> output)
        where T : ServerGameplayPacketV1;

    [TestMethod]
    public void DirectionalAdaptersMatchNativePacketTypeCatalog()
    {
        AssertClientPacketType(new JoinWorldRequest(), 0x0100);
        AssertClientPacketType(new MovementInput(1, 1, 0, 0), 0x0101);
        AssertClientPacketType(new WorldTimeSyncRequest(1), 0x0102);
        AssertClientPacketType(new ObserveWorldRequest(), 0x0104);

        Assert.AreEqual(0x0180u, WorldReady.PacketTypeValue);
        Assert.AreEqual(0x0181u, EntitySpawn.PacketTypeValue);
        Assert.AreEqual(0x0182u, ControlledEntityState.PacketTypeValue);
        Assert.AreEqual(0x0183u, EntityStateBatch.PacketTypeValue);
        Assert.AreEqual(0x0184u, EntityRemove.PacketTypeValue);
        Assert.AreEqual(0x0185u, ScoreState.PacketTypeValue);
        Assert.AreEqual(0x0186u, RoundState.PacketTypeValue);
        Assert.AreEqual(0x0187u, WorldTimeSyncResponse.PacketTypeValue);
        Assert.AreEqual(0x0188u, ControlledEntityRebind.PacketTypeValue);
        Assert.AreEqual(0x018Bu, ObserverReady.PacketTypeValue);

        Type[] serverPacketTypes =
        {
            typeof(WorldReady),
            typeof(EntitySpawn),
            typeof(ControlledEntityState),
            typeof(EntityStateBatch),
            typeof(EntityRemove),
            typeof(ScoreState),
            typeof(RoundState),
            typeof(WorldTimeSyncResponse),
            typeof(ControlledEntityRebind),
            typeof(ObserverReady),
        };
        foreach (Type serverPacketType in serverPacketTypes)
        {
            Assert.IsFalse(
                typeof(IClientGameplayPacketV1).IsAssignableFrom(serverPacketType),
                $"{serverPacketType.Name} must not be accepted by the client packet encoder.");
        }

        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            ClientGameplayPacketEncoder.Encode(
                new JoinWorldRequest(),
                Span<byte>.Empty,
                out EncodedClientGameplayPacket failedEncoding));
        Assert.AreEqual(default, failedEncoding);
    }

    [TestMethod]
    public void JoinWorldRequestMatchesNativeGoldenBytes()
    {
        AssertClientGoldenRoundTrip(
            new JoinWorldRequest(),
            0x0100,
            "0100",
            JoinWorldRequest.Decode);
    }

    [TestMethod]
    public void MovementInputMatchesNativeGoldenBytes()
    {
        MovementInput expected = new MovementInput(0x01020304, 0x11121314, 0x1234, -2);

        AssertClientGoldenRoundTrip(
            expected,
            0x0101,
            "010004030201141312113412FEFF",
            MovementInput.Decode);
    }

    [TestMethod]
    public void WorldTimeSyncRequestMatchesNativeGoldenBytes()
    {
        WorldTimeSyncRequest expected = new WorldTimeSyncRequest(0x01020304);

        AssertClientGoldenRoundTrip(
            expected,
            0x0102,
            "010004030201",
            WorldTimeSyncRequest.Decode);
    }

    [TestMethod]
    public void ObserverPacketsMatchNativeGoldenBytes()
    {
        AssertClientGoldenRoundTrip(
            new ObserveWorldRequest(),
            0x0104,
            "0100",
            ObserveWorldRequest.Decode);

        ObserverReady ready = new ObserverReady(
            0x01020304,
            60,
            -10.0f,
            -5.0f,
            10.0f,
            5.0f,
            0x11223344);
        AssertServerGoldenRoundTrip(
            ready,
            "0100040302013C000000000020C10000A0C0000020410000A04044332211",
            static (packet, output) => packet.Encode(output),
            ObserverReady.Decode);
    }

    [TestMethod]
    public void WorldReadyMatchesNativeGoldenBytes()
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
            5.0f);

        AssertServerGoldenRoundTrip(
            expected,
            "0100040302011413121124232221343332313C0000000300000002000000000020C10000A0C0000020410000A040",
            static (packet, output) => packet.Encode(output),
            WorldReady.Decode);
    }

    [TestMethod]
    public void EntitySpawnMatchesNativeGoldenBytes()
    {
        AssertServerGoldenRoundTrip(
            MakeValidPlayerSpawn(),
            "010004030201141312112423222101003433323101000000C03F00000040000080BF000000400000003F000000BF00004040",
            static (packet, output) => packet.Encode(output),
            EntitySpawn.Decode);
    }

    [TestMethod]
    public void ControlledEntityStateMatchesNativeGoldenBytes()
    {
        ControlledEntityState expected = new ControlledEntityState(
            0x01020304,
            0x11121314,
            1.0f,
            -2.0f,
            0.5f,
            -0.5f,
            3.0f);

        AssertServerGoldenRoundTrip(
            expected,
            "010004030201141312110000803F000000C00000003F000000BF00004040",
            static (packet, output) => packet.Encode(output),
            ControlledEntityState.Decode);
    }

    [TestMethod]
    public void EntityStateBatchMatchesNativeGoldenBytes()
    {
        EntityStateBatch expected = new EntityStateBatch(
            0x01020304,
            new[]
            {
                new EntityStateRecord(1, 2, 1.0f, -2.0f, 0.5f, -0.5f, 3.0f),
                new EntityStateRecord(3, 4, 10.0f, 20.0f, 1.0f, 2.0f, -1.0f),
            });

        AssertServerGoldenRoundTrip(
            expected,
            "010002000403020101000000020000000000803F000000C00000003F000000BF000040400300000004000000000020410000A0410000803F00000040000080BF",
            static (packet, output) => packet.Encode(output),
            EntityStateBatch.Decode,
            AssertEntityStateBatchEqual);
    }

    [TestMethod]
    public void EntityRemoveMatchesNativeGoldenBytes()
    {
        EntityRemove expected = new EntityRemove(
            0x01020304,
            0x11121314,
            0x21222324,
            EntityRemoveReason.Destroyed);

        AssertServerGoldenRoundTrip(
            expected,
            "01000403020114131211242322210200",
            static (packet, output) => packet.Encode(output),
            EntityRemove.Decode);
    }

    [TestMethod]
    public void ScoreStateMatchesNativeGoldenBytes()
    {
        ScoreState expected = new ScoreState(0x01020304, 0x11121314, 0x21222324);

        AssertServerGoldenRoundTrip(
            expected,
            "0100040302011413121124232221",
            static (packet, output) => packet.Encode(output),
            ScoreState.Decode);
    }

    [TestMethod]
    public void RoundStateMatchesNativeGoldenBytes()
    {
        RoundState expected = new RoundState(
            0x01020304,
            0x11121314,
            RoundPhase.Running,
            0x21222324,
            10,
            0);

        AssertServerGoldenRoundTrip(
            expected,
            "010004030201141312110200242322210A00000000000000",
            static (packet, output) => packet.Encode(output),
            RoundState.Decode);
    }

    [TestMethod]
    public void WorldTimeSyncResponseMatchesNativeGoldenBytes()
    {
        WorldTimeSyncResponse expected = new WorldTimeSyncResponse(0x01020304, 0x11121314);

        AssertServerGoldenRoundTrip(
            expected,
            "01000403020114131211",
            static (packet, output) => packet.Encode(output),
            WorldTimeSyncResponse.Decode);
    }

    [TestMethod]
    public void ControlledEntityRebindMatchesNativeGoldenBytesAndRequiresChangedKey()
    {
        ControlledEntityRebind expected = new ControlledEntityRebind(
            0x01020304,
            0x11121314,
            0x21222324,
            0x31323334,
            0x41424344,
            0x51525354);

        AssertServerGoldenRoundTrip(
            expected,
            "0100040302011413121124232221343332314443424154535251",
            static (packet, output) => packet.Encode(output),
            ControlledEntityRebind.Decode);

        byte[] output = new byte[ControlledEntityRebind.PayloadBytes];
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (expected with
            {
                ControlledEntityId = expected.PreviousEntityId,
                ControlledEntityGeneration = expected.PreviousEntityGeneration,
            }).Encode(output));
    }

    [TestMethod]
    public void PacketsRejectInvalidLengthUnsupportedVersionAndUnknownServerType()
    {
        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            JoinWorldRequest.Decode(Convert.FromHexString("0200"), out JoinWorldRequest _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            JoinWorldRequest.Decode(new byte[] { 1 }, out JoinWorldRequest _));

        AssertUnsupportedVersion<MovementInput>(
            "010004030201141312113412FEFF",
            MovementInput.Decode);
        AssertUnsupportedVersion<WorldTimeSyncRequest>(
            "010004030201",
            WorldTimeSyncRequest.Decode);
        AssertUnsupportedServerVersion<WorldReady>(
            "0100040302011413121124232221343332313C0000000300000002000000000020C10000A0C0000020410000A040",
            WorldReady.Decode);
        AssertUnsupportedServerVersion<EntitySpawn>(
            "010004030201141312112423222101003433323101000000C03F00000040000080BF000000400000003F000000BF00004040",
            EntitySpawn.Decode);
        AssertUnsupportedServerVersion<ControlledEntityState>(
            "010004030201141312110000803F000000C00000003F000000BF00004040",
            ControlledEntityState.Decode);
        AssertUnsupportedServerVersion<EntityStateBatch>(
            "010002000403020101000000020000000000803F000000C00000003F000000BF000040400300000004000000000020410000A0410000803F00000040000080BF",
            EntityStateBatch.Decode);
        AssertUnsupportedServerVersion<EntityRemove>(
            "01000403020114131211242322210200",
            EntityRemove.Decode);
        AssertUnsupportedServerVersion<ScoreState>(
            "0100040302011413121124232221",
            ScoreState.Decode);
        AssertUnsupportedServerVersion<RoundState>(
            "010004030201141312110200242322210A00000000000000",
            RoundState.Decode);
        AssertUnsupportedServerVersion<WorldTimeSyncResponse>(
            "01000403020114131211",
            WorldTimeSyncResponse.Decode);
        AssertUnsupportedServerVersion<ControlledEntityRebind>(
            "0100040302011413121124232221343332314443424154535251",
            ControlledEntityRebind.Decode);

        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            ServerGameplayPacketDecoder.Decode(0xFFFF, ReadOnlySpan<byte>.Empty, out ServerGameplayPacket? packet));
        Assert.IsNull(packet);
    }

    [TestMethod]
    public void MovementInputRejectsZeroGenerationAndReservedAxis()
    {
        byte[] output = new byte[MovementInput.PayloadBytes];

        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            new MovementInput(0, 1, 0, 0).Encode(output));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            new MovementInput(1, 1, short.MinValue, 0).Encode(output));

        byte[] invalidPayload = Convert.FromHexString("0100010000000100000000800000");
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            MovementInput.Decode(invalidPayload, out MovementInput _));
    }

    [TestMethod]
    public void WorldReadyRejectsInvalidConfigurationAndNormalizesNegativeZero()
    {
        WorldReady invalid = new WorldReady(
            1,
            1,
            1,
            1,
            60,
            0,
            2,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f);
        byte[] output = new byte[WorldReady.PayloadBytes];
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, invalid.Encode(output));

        byte[] invalidPayload = Convert.FromHexString(
            "0100010000000100000001000000010000003C0000000000000002000000000080BF000080BF0000803F0000803F");
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            WorldReady.Decode(invalidPayload, out WorldReady? _));

        WorldReady negativeZero = invalid with
        {
            SnapshotIntervalTicks = 3,
            ArenaMinY = -0.0f,
        };
        Assert.AreEqual(GameplayProtocolError.Success, negativeZero.Encode(output));
        Assert.AreEqual(0, BitConverter.SingleToInt32Bits(BitConverter.ToSingle(output, 34)));

        output[37] = 0x80;
        Assert.AreEqual(GameplayProtocolError.Success, WorldReady.Decode(output, out WorldReady? decoded));
        Assert.IsNotNull(decoded);
        Assert.AreEqual(0, BitConverter.SingleToInt32Bits(decoded.ArenaMinY));
    }

    [TestMethod]
    public void EntitySpawnRejectsInvalidEnumsAndEntitySpecificNumericRules()
    {
        byte[] output = new byte[EntitySpawn.PayloadBytes];
        EntitySpawn valid = MakeValidPlayerSpawn();

        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            (valid with { EntityKind = EntityKind.Invalid }).Encode(output));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (valid with { MaxMoveSpeed = 0.0f }).Encode(output));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (valid with
            {
                EntityKind = EntityKind.StaticObstacle,
                MaxMoveSpeed = 0.0f,
                VelocityX = 1.0f,
            }).Encode(output));

        byte[] invalidEnumPayload = Convert.FromHexString(
            "010004030201141312112423222100003433323101000000C03F00000040000080BF000000400000003F000000BF00004040");
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            EntitySpawn.Decode(invalidEnumPayload, out EntitySpawn? _));

        byte[] invalidNumericPayload = Convert.FromHexString(
            "010004030201141312112423222101003433323101000000C03F00000000000080BF000000400000003F000000BF00004040");
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            EntitySpawn.Decode(invalidNumericPayload, out EntitySpawn? _));
    }

    [TestMethod]
    public void ControlledEntityStateRejectsInvalidGenerationAndNonFiniteState()
    {
        byte[] output = new byte[ControlledEntityState.PayloadBytes];
        ControlledEntityState valid = new ControlledEntityState(1, 1, 0, 0, 0, 0, 0);

        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (valid with { ControlledEntityGeneration = 0 }).Encode(output));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (valid with { PositionX = float.NaN }).Encode(output));

        byte[] invalidPayload = Convert.FromHexString(
            "010001000000000000000000000000000000000000000000000000000000");
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            ControlledEntityState.Decode(invalidPayload, out ControlledEntityState? _));
    }

    [TestMethod]
    public void EntityStateBatchRejectsInvalidCountLengthAndRecords()
    {
        EntityStateBatch empty = new EntityStateBatch(1, Array.Empty<EntityStateRecord>());
        byte[] emptyOutput = new byte[EntityStateBatch.HeaderBytes];
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, empty.Encode(emptyOutput));

        EntityStateBatch invalidRecord = new EntityStateBatch(
            1,
            new[] { new EntityStateRecord(0, 1, 0, 0, 0, 0, 0) });
        byte[] recordOutput = new byte[EntityStateBatch.CalculatePayloadBytes(invalidRecord.Records.Count)];
        Assert.AreEqual(GameplayProtocolError.InvalidNumeric, invalidRecord.Encode(recordOutput));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            EntityStateBatch.Decode(
                Convert.FromHexString(
                    "010001000100000000000000010000000000000000000000000000000000000000000000"),
                out EntityStateBatch? _));

        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            EntityStateBatch.Decode(Convert.FromHexString("01000100"), out EntityStateBatch? _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            EntityStateBatch.Decode(Convert.FromHexString("0100250100000000"), out EntityStateBatch? _));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            EntityStateBatch.Decode(Convert.FromHexString("0100000001000000"), out EntityStateBatch? _));
    }

    [TestMethod]
    public void EntityStateBatchSupportsNativeMaximumPayload()
    {
        EntityStateRecord[] records = new EntityStateRecord[EntityStateBatch.MaxRecords];
        for (int index = 0; index < records.Length; ++index)
        {
            records[index] = new EntityStateRecord(checked((uint)(index + 1)), 1, 0, 0, 0, 0, 0);
        }

        EntityStateBatch maximum = new EntityStateBatch(123, records);
        int payloadBytes = EntityStateBatch.CalculatePayloadBytes(records.Length);
        byte[] payload = new byte[payloadBytes];

        Assert.AreEqual(8184, payloadBytes);
        Assert.AreEqual(GameplayProtocolError.Success, maximum.Encode(payload));
        Assert.AreEqual(
            GameplayProtocolError.Success,
            EntityStateBatch.Decode(payload, out EntityStateBatch? decoded));
        Assert.IsNotNull(decoded);
        AssertEntityStateBatchEqual(maximum, decoded);

        EntityStateRecord[] overMaximumRecords = new EntityStateRecord[EntityStateBatch.MaxRecords + 1];
        Array.Copy(records, overMaximumRecords, records.Length);
        overMaximumRecords[^1] = new EntityStateRecord(293, 1, 0, 0, 0, 0, 0);
        EntityStateBatch overMaximum = new EntityStateBatch(123, overMaximumRecords);
        Assert.AreEqual(
            GameplayProtocolError.InvalidLength,
            overMaximum.Encode(Span<byte>.Empty));
    }

    [TestMethod]
    public void EntityRemoveScoreAndRoundRejectInvalidSemanticValues()
    {
        byte[] removeOutput = new byte[EntityRemove.PayloadBytes];
        EntityRemove remove = new EntityRemove(1, 1, 1, EntityRemoveReason.Destroyed);
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            (remove with { Reason = EntityRemoveReason.Invalid }).Encode(removeOutput));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (remove with { EntityId = 0 }).Encode(removeOutput));
        Assert.AreEqual(GameplayProtocolError.Success, remove.Encode(removeOutput));
        removeOutput[14] = 0;
        removeOutput[15] = 0;
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            EntityRemove.Decode(removeOutput, out EntityRemove? _));

        byte[] scoreOutput = new byte[ScoreState.PayloadBytes];
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            new ScoreState(1, 0, 10).Encode(scoreOutput));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            ScoreState.Decode(
                Convert.FromHexString("010001000000000000000A000000"),
                out ScoreState? _));

        byte[] roundOutput = new byte[RoundState.PayloadBytes];
        RoundState running = new RoundState(1, 1, RoundPhase.Running, 100, 10, 0);
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            (running with { Phase = RoundPhase.Invalid }).Encode(roundOutput));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (running with { WinnerPlayerId = 1 }).Encode(roundOutput));
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            (running with { Phase = RoundPhase.Ended }).Encode(roundOutput));
        Assert.AreEqual(GameplayProtocolError.Success, running.Encode(roundOutput));
        roundOutput[10] = 0;
        roundOutput[11] = 0;
        Assert.AreEqual(
            GameplayProtocolError.InvalidEnum,
            RoundState.Decode(roundOutput, out RoundState? _));

        Assert.AreEqual(GameplayProtocolError.Success, running.Encode(roundOutput));
        roundOutput[20] = 1;
        Assert.AreEqual(
            GameplayProtocolError.InvalidNumeric,
            RoundState.Decode(roundOutput, out RoundState? _));
    }

    private static EntitySpawn MakeValidPlayerSpawn()
    {
        return new EntitySpawn(
            0x01020304,
            0x11121314,
            0x21222324,
            EntityKind.Player,
            0x31323334,
            ShapeKind.Circle,
            1.5f,
            2.0f,
            -1.0f,
            2.0f,
            0.5f,
            -0.5f,
            3.0f);
    }

    private static void AssertClientPacketType<TPacket>(TPacket packet, uint expectedPacketType)
        where TPacket : struct, IClientGameplayPacketV1
    {
        byte[] destination = new byte[64];
        Assert.AreEqual(
            GameplayProtocolError.Success,
            ClientGameplayPacketEncoder.Encode(
                packet,
                destination,
                out EncodedClientGameplayPacket encoded));
        Assert.AreEqual(expectedPacketType, encoded.PacketType);
    }

    private static void AssertClientGoldenRoundTrip<TPacket>(
        TPacket expected,
        uint expectedPacketType,
        string goldenHex,
        DecodeValuePacket<TPacket> decode)
        where TPacket : struct, IClientGameplayPacketV1
    {
        byte[] goldenPayload = Convert.FromHexString(goldenHex);
        byte[] directlyEncoded = new byte[goldenPayload.Length];

        Assert.AreEqual(GameplayProtocolError.Success, expected.Encode(directlyEncoded));
        CollectionAssert.AreEqual(goldenPayload, directlyEncoded);
        Assert.AreEqual(GameplayProtocolError.Success, decode(goldenPayload, out TPacket decoded));
        Assert.AreEqual(expected, decoded);

        Assert.AreEqual(
            GameplayProtocolError.Success,
            ClientGameplayPacketEncoder.Encode(
                expected,
                directlyEncoded,
                out EncodedClientGameplayPacket encoded));
        Assert.AreEqual(expectedPacketType, encoded.PacketType);
        Assert.AreEqual(goldenPayload.Length, encoded.PayloadByteCount);
        CollectionAssert.AreEqual(goldenPayload, directlyEncoded);
    }

    private static void AssertServerGoldenRoundTrip<TPacket>(
        TPacket expected,
        string goldenHex,
        EncodeServerPacket<TPacket> encode,
        DecodeServerPacket<TPacket> decode,
        Action<TPacket, TPacket>? assertEqual = null)
        where TPacket : ServerGameplayPacketV1
    {
        byte[] goldenPayload = Convert.FromHexString(goldenHex);
        byte[] directlyEncoded = new byte[goldenPayload.Length];

        Assert.AreEqual(GameplayProtocolError.Success, encode(expected, directlyEncoded));
        CollectionAssert.AreEqual(goldenPayload, directlyEncoded);
        Assert.AreEqual(GameplayProtocolError.Success, decode(goldenPayload, out TPacket? decoded));
        Assert.IsNotNull(decoded);
        AssertPacketEqual(expected, decoded, assertEqual);

        Assert.AreEqual(
            GameplayProtocolError.Success,
            ServerGameplayPacketDecoder.Decode(
                expected.PacketType,
                goldenPayload,
                out ServerGameplayPacket? dispatched));
        Assert.IsInstanceOfType(dispatched, typeof(TPacket));
        AssertPacketEqual(expected, (TPacket)dispatched, assertEqual);
    }

    private static void AssertUnsupportedVersion<TPacket>(
        string validGoldenHex,
        DecodeValuePacket<TPacket> decode)
        where TPacket : struct
    {
        byte[] payload = Convert.FromHexString(validGoldenHex);
        payload[0] = 0x02;

        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            decode(payload, out TPacket _));
    }

    private static void AssertUnsupportedServerVersion<TPacket>(
        string validGoldenHex,
        DecodeServerPacket<TPacket> decode)
        where TPacket : ServerGameplayPacketV1
    {
        byte[] payload = Convert.FromHexString(validGoldenHex);
        payload[0] = 0x02;

        Assert.AreEqual(
            GameplayProtocolError.UnsupportedVersion,
            decode(payload, out TPacket? _));
    }

    private static void AssertPacketEqual<TPacket>(
        TPacket expected,
        TPacket actual,
        Action<TPacket, TPacket>? assertEqual)
    {
        if (assertEqual is null)
        {
            Assert.AreEqual(expected, actual);
            return;
        }

        assertEqual(expected, actual);
    }

    private static void AssertEntityStateBatchEqual(EntityStateBatch expected, EntityStateBatch actual)
    {
        Assert.AreEqual(expected.ServerTick, actual.ServerTick);
        Assert.AreEqual(expected.Records.Count, actual.Records.Count);
        for (int index = 0; index < expected.Records.Count; ++index)
        {
            Assert.AreEqual(expected.Records[index], actual.Records[index]);
        }
    }
}
