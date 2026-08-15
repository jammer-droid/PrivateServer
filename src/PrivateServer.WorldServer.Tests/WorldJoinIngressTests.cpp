#include "pch.h"

#include "JoinWorldRequest.h"
#include "WorldJoinIngress.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        constexpr WorldSessionKey SessionKey{10};
        constexpr std::uint32_t PlayerId = 7;
        constexpr std::uint32_t CurrentServerTick = 100;
        constexpr WorldJoinConfig Config{
            20,
            1,
            2,
            -100.0f,
            -100.0f,
            100.0f,
            100.0f,
            1,
            0.5f,
            5.0f,
            0.0f,
            0.0f,
            WorldPlayerBodyConfig{WorldGrowthConfig{10.0f, 0.25f, 1.0f, 0.01f}, 8},
            7,
        };

        [[nodiscard]] std::vector<std::byte> MakePayload(const std::string_view displayName = "Player7")
        {
            std::vector<std::byte> payload(protocol::v2::JoinWorldRequest::CalculatePayloadBytes(displayName));
            const protocol::WorldProtocolError encodeResult = protocol::v2::JoinWorldRequest::Encode(
                protocol::v2::JoinWorldRequest{std::string{displayName}}, payload);
            EXPECT_EQ(encodeResult, protocol::WorldProtocolError::Success);
            return payload;
        }
    } // namespace

    TEST(WorldJoinIngressTests, PreparesControlledEntityAndCommitsSessionBindingSeparately)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        ASSERT_TRUE(sessionRegistry.TryRegister(SessionKey));
        const std::vector<std::byte> payload = MakePayload();
        WorldResult<WorldJoinBaseline> result = WorldJoinIngress::Prepare(sessionRegistry, entityManager, SessionKey,
                                                                          PlayerId, CurrentServerTick, Config, payload);
        ASSERT_TRUE(result.Succeeded());
        const WorldJoinBaseline baseline = result.TakeValue();

        WorldSession session;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &session));
        EXPECT_FALSE(session.IsJoined());
        EXPECT_EQ(session.playerId, 0u);
        EXPECT_FALSE(session.entityKey.IsValid());

        WorldEntityComponents components;
        ASSERT_TRUE(entityManager.TryReadComponents(baseline.entityHandle, &components));
        EXPECT_EQ(components.playerControl.playerId, PlayerId);
        EXPECT_EQ(components.replicationMetadata.entityKind, WorldEntityKind::Player);
        EXPECT_FLOAT_EQ(components.movementCapability.maxMoveSpeed, Config.playerMaxMoveSpeed);
        ASSERT_EQ(components.bodyTrail.SampleCount(), 2u);
        BodyTrailSample headAnchor;
        BodyTrailSample tail;
        ASSERT_TRUE(components.bodyTrail.TryRead(0, &headAnchor));
        ASSERT_TRUE(components.bodyTrail.TryRead(1, &tail));
        EXPECT_EQ(headAnchor, (BodyTrailSample{0.0f, 0.0f}));
        EXPECT_EQ(tail, (BodyTrailSample{-10.0f, 0.0f}));

        EXPECT_EQ(baseline.entitySpawn.baseline.serverTick, CurrentServerTick);
        EXPECT_EQ(baseline.entitySpawn.baseline.entityId, baseline.entityKey.entityId);
        EXPECT_EQ(baseline.entitySpawn.baseline.generation, baseline.entityKey.generation);
        EXPECT_EQ(baseline.entitySpawn.playerId, PlayerId);
        EXPECT_EQ(baseline.entitySpawn.displayName, "Player7");
        EXPECT_EQ(baseline.worldReady.playerId, PlayerId);
        EXPECT_EQ(baseline.worldReady.controlledEntityId, baseline.entityKey.entityId);
        EXPECT_EQ(baseline.worldReady.controlledEntityGeneration, baseline.entityKey.generation);
        EXPECT_EQ(baseline.worldReady.currentServerTick, CurrentServerTick);
        EXPECT_EQ(baseline.worldReady.channelId, Config.channelId);
        EXPECT_EQ(baseline.worldReady.displayName, "Player7");
        std::vector<std::byte> entitySpawnPayload(
            protocol::v2::EntitySpawn::CalculatePayloadBytes(baseline.entitySpawn.displayName));
        std::vector<std::byte> worldReadyPayload(
            protocol::v2::WorldReady::CalculatePayloadBytes(baseline.worldReady.displayName));
        EXPECT_EQ(protocol::v2::EntitySpawn::Encode(baseline.entitySpawn, entitySpawnPayload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(protocol::v2::WorldReady::Encode(baseline.worldReady, worldReadyPayload),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(entityManager.Size(), 1u);

        ASSERT_TRUE(WorldJoinIngress::Commit(sessionRegistry, entityManager, SessionKey, PlayerId, baseline));
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &session));
        EXPECT_TRUE(session.IsJoined());
        EXPECT_EQ(session.playerId, PlayerId);
        EXPECT_EQ(session.entityKey, baseline.entityKey);
        EXPECT_EQ(session.displayName, "Player7");
    }

    TEST(WorldJoinIngressTests, RejectsMalformedAndDuplicateJoinWithoutCreatingAnotherEntity)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        ASSERT_TRUE(sessionRegistry.TryRegister(SessionKey));
        const std::vector<std::byte> payload = MakePayload();
        const std::array<std::byte, 1> malformedPayload = {std::byte{1}};
        const WorldResult<WorldJoinBaseline> malformedResult = WorldJoinIngress::Prepare(
            sessionRegistry, entityManager, SessionKey, PlayerId, CurrentServerTick, Config, malformedPayload);
        ASSERT_TRUE(malformedResult.Failed());
        EXPECT_EQ(malformedResult.Error(), WorldErrorCode::MalformedPayload);
        EXPECT_EQ(entityManager.Size(), 0u);

        std::vector<std::byte> invalidDisplayNamePayload = payload;
        invalidDisplayNamePayload[protocol::v2::JoinWorldRequest::Wire::DisplayNameOffset] = std::byte{0x20};
        const WorldResult<WorldJoinBaseline> invalidDisplayNameResult = WorldJoinIngress::Prepare(
            sessionRegistry, entityManager, SessionKey, PlayerId, CurrentServerTick, Config, invalidDisplayNamePayload);
        ASSERT_TRUE(invalidDisplayNameResult.Failed());
        EXPECT_EQ(invalidDisplayNameResult.Error(), WorldErrorCode::MalformedPayload);
        EXPECT_EQ(entityManager.Size(), 0u);

        WorldResult<WorldJoinBaseline> preparedResult = WorldJoinIngress::Prepare(
            sessionRegistry, entityManager, SessionKey, PlayerId, CurrentServerTick, Config, payload);
        ASSERT_TRUE(preparedResult.Succeeded());
        const WorldJoinBaseline baseline = preparedResult.TakeValue();
        ASSERT_TRUE(WorldJoinIngress::Commit(sessionRegistry, entityManager, SessionKey, PlayerId, baseline));
        const WorldResult<WorldJoinBaseline> duplicateResult = WorldJoinIngress::Prepare(
            sessionRegistry, entityManager, SessionKey, PlayerId, CurrentServerTick, Config, payload);
        ASSERT_TRUE(duplicateResult.Failed());
        EXPECT_EQ(duplicateResult.Error(), WorldErrorCode::AlreadyExists);
        EXPECT_EQ(entityManager.Size(), 1u);
    }

    TEST(WorldJoinIngressTests, RejectsInvalidConfigBeforeMutatingWorldState)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        ASSERT_TRUE(sessionRegistry.TryRegister(SessionKey));
        const std::vector<std::byte> payload = MakePayload();
        WorldJoinConfig invalidConfig = Config;
        invalidConfig.playerMaxMoveSpeed = 0.0f;
        const WorldResult<WorldJoinBaseline> result = WorldJoinIngress::Prepare(
            sessionRegistry, entityManager, SessionKey, PlayerId, CurrentServerTick, invalidConfig, payload);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(entityManager.Size(), 0u);
    }

    TEST(WorldJoinIngressTests, RejectsObserverRoleBeforeCreatingPlayerEntity)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        ASSERT_TRUE(sessionRegistry.TryRegister(SessionKey));
        ASSERT_TRUE(sessionRegistry.TryBindObserver(SessionKey));

        const std::vector<std::byte> payload = MakePayload();
        const WorldResult<WorldJoinBaseline> result = WorldJoinIngress::Prepare(
            sessionRegistry, entityManager, SessionKey, PlayerId, CurrentServerTick, Config, payload);

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::AlreadyExists);
        EXPECT_EQ(entityManager.Size(), 0u);
    }

    TEST(WorldJoinIngressTests, RollsBackPreparedEntityWithoutJoiningSession)
    {
        WorldSessionRegistry sessionRegistry;
        WorldEntityManager entityManager;
        ASSERT_TRUE(sessionRegistry.TryRegister(SessionKey));
        const std::vector<std::byte> payload = MakePayload();
        WorldResult<WorldJoinBaseline> result = WorldJoinIngress::Prepare(sessionRegistry, entityManager, SessionKey,
                                                                          PlayerId, CurrentServerTick, Config, payload);
        ASSERT_TRUE(result.Succeeded());
        const WorldJoinBaseline baseline = result.TakeValue();
        ASSERT_TRUE(WorldJoinIngress::Rollback(entityManager, baseline));
        EXPECT_FALSE(WorldJoinIngress::Commit(sessionRegistry, entityManager, SessionKey, PlayerId, baseline));

        WorldSession session;
        ASSERT_TRUE(sessionRegistry.TryFind(SessionKey, &session));
        EXPECT_FALSE(session.IsJoined());
        EXPECT_EQ(entityManager.Size(), 0u);
    }
} // namespace psnr::world::tests
