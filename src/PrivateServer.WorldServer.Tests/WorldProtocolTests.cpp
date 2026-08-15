#include "pch.h"

#include "ControlStateCommand.h"
#include "ControlledEntityRebind.h"
#include "ControlledEntityState.h"
#include "EntityRemove.h"
#include "EntitySpawn.h"
#include "EntityStateBatch.h"
#include "JoinWorldRequest.h"
#include "MovementInput.h"
#include "RoundState.h"
#include "RoundResult.h"
#include "ScoreState.h"
#include "WorldPacketTypes.h"
#include "WorldOverviewSnapshot.h"
#include "WorldReady.h"
#include "WorldTimeSyncRequest.h"
#include "WorldTimeSyncResponse.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace psnr::world::protocol::tests
{
    static_assert(std::is_same_v<std::underlying_type_t<C2SPacketType>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<S2CPacketType>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<EntityKind>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<ShapeKind>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<EntityRemoveReason>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<RoundPhase>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<v2::TurnState>, std::uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<v2::BoostState>, std::uint16_t>);
    static_assert(!std::is_same_v<C2SPacketType, S2CPacketType>);

    namespace
    {
        template <typename Packet, std::size_t Size>
        void ExpectGoldenRoundTrip(const Packet& expected, const std::array<std::byte, Size>& goldenPayload)
        {
            std::array<std::byte, Size> encoded{};
            Packet decoded;

            const WorldProtocolError encodeError = Packet::Encode(expected, encoded);
            const WorldProtocolError decodeError = Packet::Decode(goldenPayload, &decoded);

            ASSERT_EQ(encodeError, WorldProtocolError::Success);
            ASSERT_EQ(decodeError, WorldProtocolError::Success);
            for (std::size_t index = 0; index < Size; ++index)
            {
                EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[index]),
                          std::to_integer<std::uint8_t>(goldenPayload[index]));
            }
            EXPECT_TRUE(decoded == expected);
        }

        template <typename Packet, std::size_t Size>
        void ExpectUnsupportedVersion(const std::array<std::byte, Size>& validPayload)
        {
            std::array<std::byte, Size> unsupportedPayload = validPayload;
            unsupportedPayload[0] = std::byte{0x02};
            Packet decoded;

            EXPECT_EQ(Packet::Decode(unsupportedPayload, &decoded), WorldProtocolError::UnsupportedVersion);
        }
    } // namespace

    TEST(WorldPacketTypesTests, CatalogMatchesCanonicalPacketTypes)
    {
        EXPECT_EQ(static_cast<std::uint16_t>(C2SPacketType::JoinWorldRequest), 0x0100);
        EXPECT_EQ(static_cast<std::uint16_t>(C2SPacketType::MovementInput), 0x0101);
        EXPECT_EQ(static_cast<std::uint16_t>(C2SPacketType::WorldTimeSyncRequest), 0x0102);
        EXPECT_EQ(static_cast<std::uint16_t>(C2SPacketType::ControlStateCommand), 0x0103);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::WorldReady), 0x0180);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::EntitySpawn), 0x0181);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::ControlledEntityState), 0x0182);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::EntityStateBatch), 0x0183);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::EntityRemove), 0x0184);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::ScoreState), 0x0185);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::RoundState), 0x0186);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::WorldTimeSyncResponse), 0x0187);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::ControlledEntityRebind), 0x0188);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::WorldOverviewSnapshot), 0x0189);
        EXPECT_EQ(static_cast<std::uint16_t>(S2CPacketType::RoundResult), 0x018A);

        ASSERT_EQ(C2SWorldIngressPacketTypes.size(), 4u);
        EXPECT_EQ(C2SWorldIngressPacketTypes[0], C2SPacketType::JoinWorldRequest);
        EXPECT_EQ(C2SWorldIngressPacketTypes[1], C2SPacketType::MovementInput);
        EXPECT_EQ(C2SWorldIngressPacketTypes[2], C2SPacketType::WorldTimeSyncRequest);
        EXPECT_EQ(C2SWorldIngressPacketTypes[3], C2SPacketType::ControlStateCommand);
    }

    TEST(WorldProtocolPacketTests, JoinWorldRequestMatchesGoldenBytesAndRejectsMalformedPayload)
    {
        constexpr std::array<std::byte, v1::JoinWorldRequest::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01},
            std::byte{0x00},
        };
        const std::array<std::byte, 1> shortPayload = {std::byte{0x01}};
        v1::JoinWorldRequest decoded;
        v1::JoinWorldRequest* const nullOutput = nullptr;

        ExpectGoldenRoundTrip(v1::JoinWorldRequest{}, GoldenPayload);
        ExpectUnsupportedVersion<v1::JoinWorldRequest>(GoldenPayload);
        EXPECT_EQ(v1::JoinWorldRequest::Decode(shortPayload, &decoded), WorldProtocolError::InvalidLength);
        EXPECT_EQ(v1::JoinWorldRequest::Decode(GoldenPayload, nullOutput), WorldProtocolError::InvalidArgument);
    }

    TEST(WorldProtocolPacketTests, JoinWorldRequestV2MatchesGoldenBytesAndValidatesDisplayName)
    {
        const v2::JoinWorldRequest expected{"Player7"};
        constexpr std::array<std::byte, 11> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00}, std::byte{0x50}, std::byte{0x6C},
            std::byte{0x61}, std::byte{0x79}, std::byte{0x65}, std::byte{0x72}, std::byte{0x37},
        };
        std::array<std::byte, 11> unsupportedVersion = GoldenPayload;
        unsupportedVersion[0] = std::byte{0x03};
        std::array<std::byte, 11> invalidLength = GoldenPayload;
        invalidLength[v2::JoinWorldRequest::Wire::DisplayNameByteCountOffset] = std::byte{0x08};
        std::array<std::byte, 11> invalidCharacter = GoldenPayload;
        invalidCharacter[v2::JoinWorldRequest::Wire::DisplayNameOffset + 3] = std::byte{0x20};
        std::array<std::byte, 11> invalidPunctuation = GoldenPayload;
        invalidPunctuation[v2::JoinWorldRequest::Wire::DisplayNameOffset] = std::byte{0x5F};
        std::array<std::byte, 11> invalidNonAscii = GoldenPayload;
        invalidNonAscii[v2::JoinWorldRequest::Wire::DisplayNameOffset] = std::byte{0xC3};
        std::array<std::byte, 11> output{};
        std::array<std::byte, 12> invalidNameOutput{};
        v2::JoinWorldRequest decoded;

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        EXPECT_EQ(v2::JoinWorldRequest::Decode(unsupportedVersion, &decoded),
                  WorldProtocolError::UnsupportedVersion);
        EXPECT_EQ(v2::JoinWorldRequest::Decode(invalidLength, &decoded), WorldProtocolError::InvalidLength);
        EXPECT_EQ(v2::JoinWorldRequest::Decode(invalidCharacter, &decoded), WorldProtocolError::InvalidArgument);
        EXPECT_EQ(v2::JoinWorldRequest::Decode(invalidPunctuation, &decoded), WorldProtocolError::InvalidArgument);
        EXPECT_EQ(v2::JoinWorldRequest::Decode(invalidNonAscii, &decoded), WorldProtocolError::InvalidArgument);
        EXPECT_EQ(v2::JoinWorldRequest::Encode(v2::JoinWorldRequest{"Player 7"}, invalidNameOutput),
                  WorldProtocolError::InvalidArgument);
        EXPECT_EQ(v2::JoinWorldRequest::CalculatePayloadBytes(std::string(49, 'A')), 0u);

        constexpr std::array<std::byte, v2::JoinWorldRequest::Wire::MinimumPayloadBytes> EmptyPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        };
        ExpectGoldenRoundTrip(v2::JoinWorldRequest{}, EmptyPayload);
    }

    TEST(WorldProtocolPacketTests, MovementInputMatchesGoldenBytesAndValidatesGenerationAndAxes)
    {
        static_assert(v1::MovementInput::Wire::PayloadBytes == 14);

        constexpr v1::MovementInput Expected{0x01020304, 0x11121314, 0x1234, -2};
        constexpr std::array<std::byte, v1::MovementInput::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
            std::byte{0x01}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
            std::byte{0x34}, std::byte{0x12}, std::byte{0xFE}, std::byte{0xFF},
        };
        const v1::MovementInput zeroGeneration{0, 1, 0, 0};
        const v1::MovementInput reservedAxis{1, 1, std::numeric_limits<std::int16_t>::min(), 0};
        std::array<std::byte, v1::MovementInput::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        ExpectUnsupportedVersion<v1::MovementInput>(GoldenPayload);
        EXPECT_EQ(v1::MovementInput::Encode(zeroGeneration, output), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v1::MovementInput::Encode(reservedAxis, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, ControlStateCommandMatchesGoldenBytesAndRejectsInvalidFields)
    {
        static_assert(v2::ControlStateCommand::Wire::PayloadBytes == 14);

        constexpr v2::ControlStateCommand Expected{
            0x01020304,
            0x0A0B0C0D,
            v2::TurnState::Left,
            v2::BoostState::On,
        };
        constexpr std::array<std::byte, v2::ControlStateCommand::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
            std::byte{0x01}, std::byte{0x0D}, std::byte{0x0C}, std::byte{0x0B}, std::byte{0x0A},
            std::byte{0x02}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
        };
        v2::ControlStateCommand zeroGeneration = Expected;
        zeroGeneration.controlledEntityGeneration = 0;
        v2::ControlStateCommand zeroSequence = Expected;
        zeroSequence.inputSequence = 0;
        std::array<std::byte, v2::ControlStateCommand::Wire::PayloadBytes> output{};
        std::array<std::byte, v2::ControlStateCommand::Wire::PayloadBytes> unsupportedVersion = GoldenPayload;
        unsupportedVersion[0] = std::byte{0x03};
        std::array<std::byte, v2::ControlStateCommand::Wire::PayloadBytes> invalidTurn = GoldenPayload;
        invalidTurn[v2::ControlStateCommand::Wire::TurnStateOffset] = std::byte{0x04};
        std::array<std::byte, v2::ControlStateCommand::Wire::PayloadBytes> invalidBoost = GoldenPayload;
        invalidBoost[v2::ControlStateCommand::Wire::BoostStateOffset] = std::byte{0x03};
        const std::array<std::byte, v2::ControlStateCommand::Wire::PayloadBytes - 1> shortPayload{};
        v2::ControlStateCommand decoded;
        v2::ControlStateCommand* const nullOutput = nullptr;

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        EXPECT_EQ(v2::ControlStateCommand::Encode(zeroGeneration, output), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v2::ControlStateCommand::Encode(zeroSequence, output), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v2::ControlStateCommand::Decode(unsupportedVersion, &decoded),
                  WorldProtocolError::UnsupportedVersion);
        EXPECT_EQ(v2::ControlStateCommand::Decode(invalidTurn, &decoded), WorldProtocolError::InvalidEnum);
        EXPECT_EQ(v2::ControlStateCommand::Decode(invalidBoost, &decoded), WorldProtocolError::InvalidEnum);
        EXPECT_EQ(v2::ControlStateCommand::Decode(shortPayload, &decoded), WorldProtocolError::InvalidLength);
        EXPECT_EQ(v2::ControlStateCommand::Decode(GoldenPayload, nullOutput), WorldProtocolError::InvalidArgument);
    }

    TEST(WorldProtocolPacketTests, WorldTimeSyncRequestMatchesGoldenBytes)
    {
        constexpr v1::WorldTimeSyncRequest Expected{0x01020304};
        constexpr std::array<std::byte, v1::WorldTimeSyncRequest::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
        };

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        ExpectUnsupportedVersion<v1::WorldTimeSyncRequest>(GoldenPayload);
    }

    TEST(WorldProtocolPacketTests, WorldReadyMatchesGoldenBytesAndValidatesConfiguration)
    {
        constexpr v1::WorldReady Expected{
            0x01020304, 0x11121314, 0x21222324, 0x31323334, 60, 3, 2, -10.0f, -5.0f, 10.0f, 5.0f,
        };
        constexpr std::array<std::byte, v1::WorldReady::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x34}, std::byte{0x33}, std::byte{0x32}, std::byte{0x31},
            std::byte{0x3C}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x03}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0xC1}, std::byte{0x00}, std::byte{0x00},
            std::byte{0xA0}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0x41},
            std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x40},
        };
        v1::WorldReady invalid = Expected;
        invalid.snapshotIntervalTicks = 0;
        std::array<std::byte, v1::WorldReady::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        ExpectUnsupportedVersion<v1::WorldReady>(GoldenPayload);
        EXPECT_EQ(v1::WorldReady::Encode(invalid, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, WorldReadyV2MatchesGoldenBytesAndValidatesChannelAndDisplayName)
    {
        const v2::WorldReady expected{
            0x01020304, 0x11121314, 0x21222324, 0x31323334, 60, 3, 2, -10.0f, -5.0f, 10.0f, 5.0f,
            0x11223344, "Player7",
        };
        constexpr std::array<std::byte, 59> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x34}, std::byte{0x33}, std::byte{0x32}, std::byte{0x31},
            std::byte{0x3C}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x03}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0xC1}, std::byte{0x00}, std::byte{0x00},
            std::byte{0xA0}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0x41},
            std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x40}, std::byte{0x44}, std::byte{0x33},
            std::byte{0x22}, std::byte{0x11}, std::byte{0x07}, std::byte{0x00}, std::byte{0x50}, std::byte{0x6C},
            std::byte{0x61}, std::byte{0x79}, std::byte{0x65}, std::byte{0x72}, std::byte{0x37},
        };
        std::array<std::byte, 59> invalidCharacter = GoldenPayload;
        invalidCharacter[v2::WorldReady::Wire::DisplayNameOffset] = std::byte{0x5F};
        std::array<std::byte, 59> output{};
        v2::WorldReady decoded;

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        EXPECT_EQ(v2::WorldReady::Decode(invalidCharacter, &decoded), WorldProtocolError::InvalidArgument);
        EXPECT_EQ(v2::WorldReady::Encode(v2::WorldReady{expected.playerId,
                                                       expected.controlledEntityId,
                                                       expected.controlledEntityGeneration,
                                                       expected.currentServerTick,
                                                       expected.tickRateHz,
                                                       expected.snapshotIntervalTicks,
                                                       expected.commandSlackTicks,
                                                       expected.arenaMinX,
                                                       expected.arenaMinY,
                                                       expected.arenaMaxX,
                                                       expected.arenaMaxY,
                                                       0,
                                                       expected.displayName},
                                        output),
                  WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, WorldReadyNormalizesNegativeZero)
    {
        v1::WorldReady value{1, 1, 1, 1, 60, 3, 2, -1.0f, -0.0f, 1.0f, 1.0f};
        std::array<std::byte, v1::WorldReady::Wire::PayloadBytes> payload{};
        v1::WorldReady decoded;

        ASSERT_EQ(v1::WorldReady::Encode(value, payload), WorldProtocolError::Success);
        payload[v1::WorldReady::Wire::ArenaMinYOffset + 3] = std::byte{0x80};
        ASSERT_EQ(v1::WorldReady::Decode(payload, &decoded), WorldProtocolError::Success);
        EXPECT_FALSE(std::signbit(decoded.arenaMinY));
    }

    TEST(WorldProtocolPacketTests, ControlledEntityRebindMatchesGoldenBytesAndRequiresChangedKey)
    {
        constexpr v1::ControlledEntityRebind Expected{
            0x01020304,
            0x11121314,
            0x21222324,
            0x31323334,
            0x41424344,
            0x51525354,
        };
        constexpr std::array<std::byte, v1::ControlledEntityRebind::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x34}, std::byte{0x33}, std::byte{0x32}, std::byte{0x31},
            std::byte{0x44}, std::byte{0x43}, std::byte{0x42}, std::byte{0x41}, std::byte{0x54}, std::byte{0x53},
            std::byte{0x52}, std::byte{0x51},
        };
        v1::ControlledEntityRebind unchanged = Expected;
        unchanged.controlledEntityId = unchanged.previousEntityId;
        unchanged.controlledEntityGeneration = unchanged.previousEntityGeneration;
        std::array<std::byte, v1::ControlledEntityRebind::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        ExpectUnsupportedVersion<v1::ControlledEntityRebind>(GoldenPayload);
        EXPECT_EQ(v1::ControlledEntityRebind::Encode(unchanged, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, EntityStateBatchMatchesGoldenBytesAndMaximumSize)
    {
        const v1::EntityStateBatch Expected{
            0x01020304,
            {
                {1, 2, 1.0f, -2.0f, 0.5f, -0.5f, 3.0f},
                {3, 4, 10.0f, 20.0f, 1.0f, 2.0f, -1.0f},
            },
        };
        constexpr std::array<std::byte, v1::EntityStateBatch::Wire::CalculatePayloadBytes(2)> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03},
            std::byte{0x02}, std::byte{0x01}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x40},
            std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0x41},
            std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x41}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xBF},
        };

        ExpectGoldenRoundTrip(Expected, GoldenPayload);

        v1::EntityStateBatch maximum;
        maximum.serverTick = 123;
        maximum.records.reserve(v1::EntityStateBatch::Wire::MaxRecords);
        for (std::size_t index = 0; index < v1::EntityStateBatch::Wire::MaxRecords; ++index)
        {
            maximum.records.push_back({static_cast<std::uint32_t>(index + 1), 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        }
        std::vector<std::byte> payload(v1::EntityStateBatch::Wire::CalculatePayloadBytes(maximum.records.size()));
        v1::EntityStateBatch decoded;

        ASSERT_EQ(payload.size(), 8184u);
        ASSERT_EQ(v1::EntityStateBatch::Encode(maximum, payload), WorldProtocolError::Success);
        std::vector<std::byte> spanPayload(payload.size());
        ASSERT_EQ(v1::EntityStateBatch::Encode(maximum.serverTick, maximum.records, spanPayload),
                  WorldProtocolError::Success);
        EXPECT_EQ(spanPayload, payload);
        ASSERT_EQ(v1::EntityStateBatch::Decode(payload, &decoded), WorldProtocolError::Success);
        EXPECT_TRUE(decoded == maximum);

        v1::EntityStateBatch overMaximum = maximum;
        overMaximum.records.push_back({293, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        std::vector<std::byte> overMaximumOutput;
        EXPECT_EQ(v1::EntityStateBatch::Encode(overMaximum, overMaximumOutput), WorldProtocolError::InvalidLength);
    }

    TEST(WorldProtocolPacketTests, EntityStateBatchRejectsInvalidCountLengthAndRecords)
    {
        const v1::EntityStateBatch empty{1, {}};
        const v1::EntityStateBatch invalidRecord{
            1,
            {
                {0, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            },
        };
        std::array<std::byte, v1::EntityStateBatch::Wire::HeaderBytes> emptyOutput{};
        std::array<std::byte, v1::EntityStateBatch::Wire::CalculatePayloadBytes(1)> recordOutput{};
        const std::array<std::byte, v1::EntityStateBatch::Wire::HeaderBytes> inconsistentPayload = {
            std::byte{0x01},
            std::byte{0x00},
            std::byte{0x01},
            std::byte{0x00},
        };
        const std::array<std::byte, v1::EntityStateBatch::Wire::HeaderBytes> overMaxCountPayload = {
            std::byte{0x01},
            std::byte{0x00},
            std::byte{0x25},
            std::byte{0x01},
        };
        v1::EntityStateBatch decoded;

        EXPECT_EQ(v1::EntityStateBatch::Encode(empty, emptyOutput), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v1::EntityStateBatch::Encode(invalidRecord, recordOutput), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v1::EntityStateBatch::Decode(inconsistentPayload, &decoded), WorldProtocolError::InvalidLength);
        EXPECT_EQ(v1::EntityStateBatch::Decode(overMaxCountPayload, &decoded), WorldProtocolError::InvalidLength);
    }

    TEST(WorldProtocolPacketTests, EntityStateBatchLeavesOrderingToReplicationPlanner)
    {
        const v1::EntityStateBatch unsorted{
            1,
            {
                {2, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                {1, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            },
        };
        std::array<std::byte, v1::EntityStateBatch::Wire::CalculatePayloadBytes(2)> payload{};
        v1::EntityStateBatch decoded;

        ASSERT_EQ(v1::EntityStateBatch::Encode(unsorted, payload), WorldProtocolError::Success);
        ASSERT_EQ(v1::EntityStateBatch::Decode(payload, &decoded), WorldProtocolError::Success);
        EXPECT_TRUE(decoded == unsorted);
    }

    TEST(WorldProtocolPacketTests, EntityStateBatchV2MatchesGoldenBytesAndKeepsSnakeRecordAtomic)
    {
        const v2::EntityStateBatch expected{
            0x01020304, 0x11121314, 0, 2,
            {v2::EntityStateRecord{
                7, 9, 1.0f, -2.0f, 3.0f, 1.5f, 2, v2::BoostState::On,
                {v2::EntityStateBodySample{0.5f, -0.5f}, v2::EntityStateBodySample{4.0f, -4.0f}},
            }},
        };
        constexpr std::array<std::byte, 64> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x02}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x09}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x3F}, std::byte{0x02}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xC0},
        };

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        EXPECT_EQ(v2::EntityStateBatch::Wire::CalculatePayloadBytes(expected.records), GoldenPayload.size());

        v2::EntityStateBatch invalidChunk = expected;
        invalidChunk.chunkIndex = invalidChunk.chunkCount;
        v2::EntityStateBatch invalidEnum = expected;
        invalidEnum.records[0].boostState = v2::BoostState::Invalid;
        v2::EntityStateBatch emptyBody = expected;
        emptyBody.records[0].bodyTrailSamples.clear();
        std::array<std::byte, GoldenPayload.size()> output{};
        std::array<std::byte, v2::EntityStateBatch::Wire::HeaderBytes + v2::EntityStateRecord::Wire::HeaderBytes>
            emptyBodyOutput{};

        EXPECT_EQ(v2::EntityStateBatch::Encode(invalidChunk, output), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v2::EntityStateBatch::Encode(invalidEnum, output), WorldProtocolError::InvalidEnum);
        EXPECT_EQ(v2::EntityStateBatch::Encode(emptyBody, emptyBodyOutput), WorldProtocolError::InvalidNumeric);

        std::array<std::byte, GoldenPayload.size()> invalidBodyCount = GoldenPayload;
        invalidBodyCount[v2::EntityStateBatch::Wire::HeaderBytes +
                         v2::EntityStateRecord::Wire::BodySampleCountOffset] = std::byte{0x03};
        v2::EntityStateBatch decoded;
        EXPECT_EQ(v2::EntityStateBatch::Decode(invalidBodyCount, &decoded), WorldProtocolError::InvalidLength);
    }

    TEST(WorldProtocolPacketTests, EntitySpawnMatchesGoldenBytesAndValidatesSemanticFields)
    {
        constexpr v1::EntitySpawn Expected{
            0x01020304, 0x11121314,
            0x21222324, EntityKind::Player,
            0x31323334, ShapeKind::Circle,
            1.5f,       2.0f,
            -1.0f,      2.0f,
            0.5f,       -0.5f,
            3.0f,
        };
        constexpr std::array<std::byte, v1::EntitySpawn::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x01}, std::byte{0x00}, std::byte{0x34}, std::byte{0x33},
            std::byte{0x32}, std::byte{0x31}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0xC0}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x40}, std::byte{0x40},
        };
        v1::EntitySpawn invalidEnum = Expected;
        invalidEnum.entityKind = EntityKind::Invalid;
        v1::EntitySpawn invalidPlayerSpeed = Expected;
        invalidPlayerSpeed.maxMoveSpeed = 0.0f;
        std::array<std::byte, v1::EntitySpawn::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        ExpectUnsupportedVersion<v1::EntitySpawn>(GoldenPayload);
        EXPECT_EQ(v1::EntitySpawn::Encode(invalidEnum, output), WorldProtocolError::InvalidEnum);
        EXPECT_EQ(v1::EntitySpawn::Encode(invalidPlayerSpeed, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, ControlledEntityStateMatchesGoldenBytesAndValidatesState)
    {
        constexpr v1::ControlledEntityState Expected{0x01020304, 0x11121314, 1.0f, -2.0f, 0.5f, -0.5f, 3.0f};
        constexpr std::array<std::byte, v1::ControlledEntityState::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x40},
        };
        v1::ControlledEntityState invalid = Expected;
        invalid.controlledEntityGeneration = 0;
        std::array<std::byte, v1::ControlledEntityState::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        EXPECT_EQ(v1::ControlledEntityState::Encode(invalid, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, ControlledEntityStateV2MatchesGoldenBytesAndValidatesBody)
    {
        const v2::ControlledEntityState expected{
            0x01020304,
            0x11121314,
            0x21222324,
            1.0f,
            -2.0f,
            3.0f,
            1.5f,
            2,
            v2::BoostState::On,
            {
                v2::ControlledEntityBodySample{0.5f, -0.5f},
                v2::ControlledEntityBodySample{4.0f, -4.0f},
            },
        };
        constexpr std::array<std::byte, v2::ControlledEntityState::Wire::CalculatePayloadBytes(2)> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x40}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x3F},
            std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
            std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x80}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xC0},
        };

        ExpectGoldenRoundTrip(expected, GoldenPayload);

        std::array<std::byte, GoldenPayload.size()> unsupportedVersion = GoldenPayload;
        unsupportedVersion[0] = std::byte{0x01};
        v2::ControlledEntityState decoded;
        EXPECT_EQ(v2::ControlledEntityState::Decode(unsupportedVersion, &decoded),
                  WorldProtocolError::UnsupportedVersion);

        v2::ControlledEntityState invalidEnum = expected;
        invalidEnum.boostState = v2::BoostState::Invalid;
        v2::ControlledEntityState invalidNumeric = expected;
        invalidNumeric.diameter = 0.0f;
        v2::ControlledEntityState invalidBodySample = expected;
        invalidBodySample.bodyTrailSamples[0].positionX = std::numeric_limits<float>::quiet_NaN();
        v2::ControlledEntityState emptyBody = expected;
        emptyBody.bodyTrailSamples.clear();
        std::array<std::byte, GoldenPayload.size()> output{};
        std::array<std::byte, v2::ControlledEntityState::Wire::HeaderBytes> emptyBodyOutput{};

        EXPECT_EQ(v2::ControlledEntityState::Encode(invalidEnum, output), WorldProtocolError::InvalidEnum);
        EXPECT_EQ(v2::ControlledEntityState::Encode(invalidNumeric, output), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v2::ControlledEntityState::Encode(invalidBodySample, output),
                  WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v2::ControlledEntityState::Encode(emptyBody, emptyBodyOutput), WorldProtocolError::InvalidNumeric);

        std::array<std::byte, GoldenPayload.size()> invalidCount = GoldenPayload;
        invalidCount[v2::ControlledEntityState::Wire::BodySampleCountOffset] = std::byte{0x03};
        EXPECT_EQ(v2::ControlledEntityState::Decode(invalidCount, &decoded), WorldProtocolError::InvalidLength);
        EXPECT_EQ(v2::ControlledEntityState::Wire::CalculatePayloadBytes(
                      v2::ControlledEntityState::Wire::MaximumBodySampleCount),
                  8182u);
        EXPECT_EQ(v2::ControlledEntityState::Wire::CalculatePayloadBytes(
                      v2::ControlledEntityState::Wire::MaximumBodySampleCount + 1),
                  0u);
    }

    TEST(WorldProtocolPacketTests, WorldOverviewSnapshotV2MatchesGoldenBytesAndValidatesChunk)
    {
        const v2::WorldOverviewSnapshot expected{
            0x01020304, 0x11121314, 0, 2, -10.0f, -5.0f, 10.0f, 5.0f, 1.0f, 2.0f, 3.0f,
            {v2::WorldOverviewPlayer{7, 9, {{1.0f, 2.0f}, {0.0f, 2.0f}}}},
            {v2::WorldOverviewLeaderboardEntry{1, 7, 9}},
        };
        constexpr std::array<std::byte, 82> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}, std::byte{0x14}, std::byte{0x13},
            std::byte{0x12}, std::byte{0x11}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x20}, std::byte{0xC1}, std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x20}, std::byte{0x41}, std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x40}, std::byte{0x40}, std::byte{0x01}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x09}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x01}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x09}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
        };

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        v2::WorldOverviewSnapshot invalidChunk = expected;
        invalidChunk.chunkIndex = invalidChunk.chunkCount;
        std::array<std::byte, GoldenPayload.size()> output{};
        EXPECT_EQ(v2::WorldOverviewSnapshot::Encode(invalidChunk, output), WorldProtocolError::InvalidNumeric);
        v2::WorldOverviewSnapshot laterChunkWithLeaderboard = expected;
        laterChunkWithLeaderboard.chunkIndex = 1;
        EXPECT_EQ(v2::WorldOverviewSnapshot::Encode(laterChunkWithLeaderboard, output),
                  WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, EntitySpawnV2MatchesGoldenBytesAndValidatesPlayerIdentity)
    {
        const v2::EntitySpawn expected{
            v1::EntitySpawn{
                0x01020304, 0x11121314,
                0x21222324, EntityKind::Player,
                0x31323334, ShapeKind::Circle,
                1.5f,       2.0f,
                -1.0f,      2.0f,
                0.5f,       -0.5f,
                3.0f,
            },
            0x41424344,
            "Player7",
        };
        constexpr std::array<std::byte, 63> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x01}, std::byte{0x00}, std::byte{0x34}, std::byte{0x33},
            std::byte{0x32}, std::byte{0x31}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0xC0}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xBF}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x40}, std::byte{0x40}, std::byte{0x44}, std::byte{0x43}, std::byte{0x42}, std::byte{0x41},
            std::byte{0x07}, std::byte{0x00}, std::byte{0x50}, std::byte{0x6C}, std::byte{0x61}, std::byte{0x79},
            std::byte{0x65}, std::byte{0x72}, std::byte{0x37},
        };

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        v2::EntitySpawn missingPlayerId = expected;
        missingPlayerId.playerId = 0;
        std::array<std::byte, GoldenPayload.size()> output{};
        EXPECT_EQ(v2::EntitySpawn::Encode(missingPlayerId, output), WorldProtocolError::InvalidNumeric);
        v2::EntitySpawn invalidName = expected;
        invalidName.displayName = "Player 7";
        std::array<std::byte, GoldenPayload.size() + 1> invalidOutput{};
        EXPECT_EQ(v2::EntitySpawn::Encode(invalidName, invalidOutput), WorldProtocolError::InvalidArgument);
    }

    TEST(WorldProtocolPacketTests, WorldOverviewSnapshotV3MatchesGoldenBytesAndValidatesDisplayName)
    {
        const v3::WorldOverviewSnapshot expected{
            0x01020304, 0x11121314, 0, 2, -10.0f, -5.0f, 10.0f, 5.0f, 1.0f, 2.0f, 3.0f,
            {v3::WorldOverviewPlayer{7, 9, {{1.0f, 2.0f}, {0.0f, 2.0f}}}},
            {v3::WorldOverviewLeaderboardEntry{1, 7, 9, "Player7"}},
        };
        constexpr std::array<std::byte, 91> GoldenPayload = {
            std::byte{0x03}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}, std::byte{0x14}, std::byte{0x13},
            std::byte{0x12}, std::byte{0x11}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x20}, std::byte{0xC1}, std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x20}, std::byte{0x41}, std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x40}, std::byte{0x40}, std::byte{0x01}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x09}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
            std::byte{0x01}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x09}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x07}, std::byte{0x00}, std::byte{0x50}, std::byte{0x6C}, std::byte{0x61}, std::byte{0x79},
            std::byte{0x65}, std::byte{0x72}, std::byte{0x37},
        };

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        v3::WorldOverviewSnapshot invalidName = expected;
        invalidName.leaderboard[0].displayName = "Player 7";
        std::array<std::byte, GoldenPayload.size() + 1> invalidOutput{};
        EXPECT_EQ(v3::WorldOverviewSnapshot::Encode(invalidName, invalidOutput), WorldProtocolError::InvalidArgument);

        std::array<std::byte, GoldenPayload.size()> truncatedName = GoldenPayload;
        truncatedName[82] = std::byte{0x08};
        v3::WorldOverviewSnapshot decoded;
        EXPECT_EQ(v3::WorldOverviewSnapshot::Decode(truncatedName, &decoded), WorldProtocolError::InvalidLength);
        std::array<std::byte, GoldenPayload.size()> invalidCharacter = GoldenPayload;
        invalidCharacter[84] = std::byte{0x2D};
        EXPECT_EQ(v3::WorldOverviewSnapshot::Decode(invalidCharacter, &decoded),
                  WorldProtocolError::InvalidArgument);
    }

    TEST(WorldProtocolPacketTests, RoundResultV2MatchesGoldenBytesAndValidatesWinnerList)
    {
        const v2::RoundResult expected{
            0x01020304,
            0x11121314,
            0x21222324,
            0x31323334,
            {0x41424344, 0x51525354},
        };
        constexpr std::array<std::byte, 28> GoldenPayload = {
            std::byte{0x02}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x34}, std::byte{0x33}, std::byte{0x32}, std::byte{0x31},
            std::byte{0x02}, std::byte{0x00}, std::byte{0x44}, std::byte{0x43}, std::byte{0x42}, std::byte{0x41},
            std::byte{0x54}, std::byte{0x53}, std::byte{0x52}, std::byte{0x51},
        };
        std::array<std::byte, GoldenPayload.size()> output{};

        ExpectGoldenRoundTrip(expected, GoldenPayload);
        EXPECT_EQ(v2::RoundResult::Wire::CalculatePayloadBytes(expected.winnerPlayerIds.size()),
                  GoldenPayload.size());
        EXPECT_EQ(v2::RoundResult::Wire::CalculatePayloadBytes(v2::RoundResult::Wire::MaximumWinnerCount), 8184u);
        EXPECT_EQ(v2::RoundResult::Wire::CalculatePayloadBytes(v2::RoundResult::Wire::MaximumWinnerCount + 1), 0u);

        const v2::RoundResult noWinner{1, 1, 0, 0, {}};
        std::array<std::byte, v2::RoundResult::Wire::HeaderBytes> noWinnerPayload{};
        EXPECT_EQ(v2::RoundResult::Encode(noWinner, noWinnerPayload), WorldProtocolError::Success);

        v2::RoundResult unsorted = expected;
        unsorted.winnerPlayerIds = {2, 1};
        EXPECT_EQ(v2::RoundResult::Encode(unsorted, output), WorldProtocolError::InvalidNumeric);
        v2::RoundResult zeroWinner = expected;
        zeroWinner.winnerPlayerIds = {0, 1};
        EXPECT_EQ(v2::RoundResult::Encode(zeroWinner, output), WorldProtocolError::InvalidNumeric);
        v2::RoundResult duplicateWinner = expected;
        duplicateWinner.winnerPlayerIds = {1, 1};
        EXPECT_EQ(v2::RoundResult::Encode(duplicateWinner, output), WorldProtocolError::InvalidNumeric);
        v2::RoundResult invalidNoWinner = noWinner;
        invalidNoWinner.winningGrowthPoint = 1;
        EXPECT_EQ(v2::RoundResult::Encode(invalidNoWinner, noWinnerPayload), WorldProtocolError::InvalidNumeric);

        std::array<std::byte, GoldenPayload.size()> unsupportedVersion = GoldenPayload;
        unsupportedVersion[0] = std::byte{0x01};
        v2::RoundResult decoded;
        EXPECT_EQ(v2::RoundResult::Decode(unsupportedVersion, &decoded), WorldProtocolError::UnsupportedVersion);
        std::array<std::byte, GoldenPayload.size()> invalidCount = GoldenPayload;
        invalidCount[v2::RoundResult::Wire::WinnerCountOffset] = std::byte{0x03};
        EXPECT_EQ(v2::RoundResult::Decode(invalidCount, &decoded), WorldProtocolError::InvalidLength);
    }

    TEST(WorldProtocolPacketTests, EntityRemoveMatchesGoldenBytesAndValidatesReason)
    {
        constexpr v1::EntityRemove Expected{
            0x01020304,
            0x11121314,
            0x21222324,
            EntityRemoveReason::Destroyed,
        };
        constexpr std::array<std::byte, v1::EntityRemove::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24}, std::byte{0x23},
            std::byte{0x22}, std::byte{0x21}, std::byte{0x02}, std::byte{0x00},
        };
        v1::EntityRemove invalid = Expected;
        invalid.reason = EntityRemoveReason::Invalid;
        std::array<std::byte, v1::EntityRemove::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        EXPECT_EQ(v1::EntityRemove::Encode(invalid, output), WorldProtocolError::InvalidEnum);
    }

    TEST(WorldProtocolPacketTests, ScoreStateMatchesGoldenBytesAndValidatesPlayer)
    {
        constexpr v1::ScoreState Expected{0x01020304, 0x11121314, 0x21222324};
        constexpr std::array<std::byte, v1::ScoreState::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
            std::byte{0x01}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
            std::byte{0x24}, std::byte{0x23}, std::byte{0x22}, std::byte{0x21},
        };
        v1::ScoreState invalid = Expected;
        invalid.playerId = 0;
        std::array<std::byte, v1::ScoreState::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        EXPECT_EQ(v1::ScoreState::Encode(invalid, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, RoundStateMatchesGoldenBytesAndValidatesWinnerRules)
    {
        constexpr v1::RoundState Expected{
            0x01020304, 0x11121314, RoundPhase::Running, 0x21222324, 10, 0,
        };
        constexpr std::array<std::byte, v1::RoundState::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
            std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x02}, std::byte{0x00},
            std::byte{0x24}, std::byte{0x23}, std::byte{0x22}, std::byte{0x21}, std::byte{0x0A}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        };
        v1::RoundState runningWithWinner = Expected;
        runningWithWinner.winnerPlayerId = 1;
        v1::RoundState endedWithoutWinner = Expected;
        endedWithoutWinner.phase = RoundPhase::Ended;
        std::array<std::byte, v1::RoundState::Wire::PayloadBytes> output{};

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        EXPECT_EQ(v1::RoundState::Encode(runningWithWinner, output), WorldProtocolError::InvalidNumeric);
        EXPECT_EQ(v1::RoundState::Encode(endedWithoutWinner, output), WorldProtocolError::InvalidNumeric);
    }

    TEST(WorldProtocolPacketTests, WorldTimeSyncResponseMatchesGoldenBytes)
    {
        constexpr v1::WorldTimeSyncResponse Expected{0x01020304, 0x11121314};
        constexpr std::array<std::byte, v1::WorldTimeSyncResponse::Wire::PayloadBytes> GoldenPayload = {
            std::byte{0x01}, std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
            std::byte{0x01}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
        };

        ExpectGoldenRoundTrip(Expected, GoldenPayload);
        ExpectUnsupportedVersion<v1::WorldTimeSyncResponse>(GoldenPayload);
    }
} // namespace psnr::world::protocol::tests
