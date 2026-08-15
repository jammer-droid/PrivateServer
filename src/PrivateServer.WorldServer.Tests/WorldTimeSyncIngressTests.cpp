#include "pch.h"

#include "WorldTimeSyncIngress.h"
#include "WorldTimeSyncRequest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world::tests
{
    namespace
    {
        using TimeSyncPayload = std::array<std::byte, protocol::v1::WorldTimeSyncRequest::Wire::PayloadBytes>;

        void EncodeTimeSync(const std::uint32_t probeSequence, TimeSyncPayload* const outPayload)
        {
            ASSERT_NE(outPayload, nullptr);

            const protocol::v1::WorldTimeSyncRequest request{probeSequence};
            ASSERT_EQ(protocol::v1::WorldTimeSyncRequest::Encode(request, *outPayload),
                      protocol::WorldProtocolError::Success);
        }

        void RegisterJoinedSession(WorldSessionRegistry* const registry,
                                   const WorldSessionKey sessionKey,
                                   const std::uint32_t playerId,
                                   const WorldEntityKey entityKey)
        {
            ASSERT_NE(registry, nullptr);
            ASSERT_TRUE(registry->TryRegister(sessionKey));
            ASSERT_TRUE(registry->TryBindPlayer(sessionKey, playerId, entityKey));
        }
    } // namespace

    TEST(WorldTimeSyncIngressTests, AcceptsResponseUsingLastCompletedServerTick)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr std::uint32_t ProbeSequence = 77;
        constexpr std::uint32_t LastCompletedServerTick = 100;
        protocol::v1::WorldTimeSyncResponse response;

        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};
        {
            TimeSyncPayload payload;
            EncodeTimeSync(ProbeSequence, &payload);

            ASSERT_EQ(WorldTimeSyncIngress::Admit(context, LastCompletedServerTick, payload, &response),
                      WorldIngressAdmissionResult::Accepted);
            payload.fill(std::byte{0});
        }

        EXPECT_EQ(response.probeSequence, ProbeSequence);
        EXPECT_EQ(response.serverTick, LastCompletedServerTick);
    }

    TEST(WorldTimeSyncIngressTests, RejectsMissingAndUnjoinedSessions)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey MissingSessionKey{10};
        constexpr WorldSessionKey UnjoinedSessionKey{11};
        TimeSyncPayload payload;
        protocol::v1::WorldTimeSyncResponse response;
        EncodeTimeSync(77, &payload);

        const WorldIngressAdmissionContext missingContext{registry, MissingSessionKey};
        EXPECT_EQ(WorldTimeSyncIngress::Admit(missingContext, 100, payload, &response),
                  WorldIngressAdmissionResult::SessionNotFound);

        ASSERT_TRUE(registry.TryRegister(UnjoinedSessionKey));
        const WorldIngressAdmissionContext unjoinedContext{registry, UnjoinedSessionKey};
        EXPECT_EQ(WorldTimeSyncIngress::Admit(unjoinedContext, 100, payload, &response),
                  WorldIngressAdmissionResult::SessionNotJoined);
    }

    TEST(WorldTimeSyncIngressTests, RejectsMalformedAndNullOutputWithoutChangingResponse)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        const protocol::v1::WorldTimeSyncResponse unchanged{77, 100};
        protocol::v1::WorldTimeSyncResponse response = unchanged;
        TimeSyncPayload payload;

        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        EncodeTimeSync(77, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        const std::span<const std::byte> malformedPayload{payload.data(), payload.size() - 1};
        EXPECT_EQ(WorldTimeSyncIngress::Admit(context, 100, malformedPayload, &response),
                  WorldIngressAdmissionResult::MalformedPayload);
        EXPECT_EQ(response, unchanged);

        EXPECT_EQ(WorldTimeSyncIngress::Admit(context, 100, payload, nullptr),
                  WorldIngressAdmissionResult::InvalidArgument);
        EXPECT_EQ(response, unchanged);
    }
} // namespace psnr::world::tests
