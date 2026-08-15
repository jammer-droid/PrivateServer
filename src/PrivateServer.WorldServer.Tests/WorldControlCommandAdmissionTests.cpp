#include "pch.h"

#include "ControlStateCommand.h"
#include "WorldControlCommandAdmission.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace psnr::world::tests
{
    namespace
    {
        using ControlPayload = std::array<std::byte, protocol::v2::ControlStateCommand::Wire::PayloadBytes>;

        void EncodeControl(const std::uint32_t controlledEntityGeneration, const std::uint32_t inputSequence,
                           const protocol::v2::TurnState turnState, const protocol::v2::BoostState boostState,
                           ControlPayload* const outPayload)
        {
            ASSERT_NE(outPayload, nullptr);

            const protocol::v2::ControlStateCommand input{
                controlledEntityGeneration,
                inputSequence,
                turnState,
                boostState,
            };
            ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(input, *outPayload),
                      protocol::WorldProtocolError::Success);
        }

        void RegisterJoinedSession(WorldSessionRegistry* const registry, const WorldSessionKey sessionKey,
                                   const std::uint32_t playerId, const WorldEntityKey entityKey)
        {
            ASSERT_NE(registry, nullptr);
            ASSERT_TRUE(registry->TryRegister(sessionKey));
            ASSERT_TRUE(registry->TryBindPlayer(sessionKey, playerId, entityKey));
        }
    } // namespace

    static_assert(std::is_trivially_copyable_v<WorldControlCommand>);

    TEST(WorldControlCommandAdmissionTests, AcceptsOwnedCommandUsingCurrentSessionBinding)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr std::uint32_t PlayerId = 7;
        WorldControlCommand command;

        RegisterJoinedSession(&registry, SessionKey, PlayerId, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};
        {
            ControlPayload payload;
            EncodeControl(EntityKey.generation, 11, protocol::v2::TurnState::Left,
                          protocol::v2::BoostState::On, &payload);

            ASSERT_EQ(WorldControlCommandAdmission::Admit(context, 100, payload, &command),
                      WorldIngressAdmissionResult::Accepted);
            payload.fill(std::byte{0});
        }

        EXPECT_EQ(command.sessionKey, SessionKey);
        EXPECT_EQ(command.playerId, PlayerId);
        EXPECT_EQ(command.entityKey, EntityKey);
        EXPECT_EQ(command.admittedServerTick, 100u);
        EXPECT_EQ(command.inputSequence, 11u);
        EXPECT_EQ(command.turnState, WorldTurnState::Left);
        EXPECT_EQ(command.boostState, WorldBoostState::On);
    }

    TEST(WorldControlCommandAdmissionTests, RejectsMissingUnjoinedAndStaleSessionBinding)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey MissingSessionKey{10};
        constexpr WorldSessionKey UnjoinedSessionKey{11};
        constexpr WorldSessionKey JoinedSessionKey{12};
        constexpr WorldEntityKey EntityKey{3, 2};
        ControlPayload payload;
        WorldControlCommand command;
        EncodeControl(1, 11, protocol::v2::TurnState::Straight, protocol::v2::BoostState::Off, &payload);

        const WorldIngressAdmissionContext missingContext{registry, MissingSessionKey};
        EXPECT_EQ(WorldControlCommandAdmission::Admit(missingContext, 100, payload, &command),
                  WorldIngressAdmissionResult::SessionNotFound);

        ASSERT_TRUE(registry.TryRegister(UnjoinedSessionKey));
        const WorldIngressAdmissionContext unjoinedContext{registry, UnjoinedSessionKey};
        EXPECT_EQ(WorldControlCommandAdmission::Admit(unjoinedContext, 100, payload, &command),
                  WorldIngressAdmissionResult::SessionNotJoined);

        RegisterJoinedSession(&registry, JoinedSessionKey, 7, EntityKey);
        const WorldIngressAdmissionContext joinedContext{registry, JoinedSessionKey};
        EXPECT_EQ(WorldControlCommandAdmission::Admit(joinedContext, 100, payload, &command),
                  WorldIngressAdmissionResult::StaleEntityGeneration);
    }

    TEST(WorldControlCommandAdmissionTests, RejectsMalformedAndNullOutputWithoutChangingCommand)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr WorldControlCommand Unchanged{
            SessionKey, 7, EntityKey, 100, 11, WorldTurnState::Right, WorldBoostState::On,
        };
        WorldControlCommand command = Unchanged;
        ControlPayload payload;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        EncodeControl(EntityKey.generation, 12, protocol::v2::TurnState::Left,
                      protocol::v2::BoostState::Off, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        const std::span<const std::byte> malformedPayload{payload.data(), payload.size() - 1};
        EXPECT_EQ(WorldControlCommandAdmission::Admit(context, 101, malformedPayload, &command),
                  WorldIngressAdmissionResult::MalformedPayload);
        EXPECT_EQ(command, Unchanged);

        EXPECT_EQ(WorldControlCommandAdmission::Admit(context, 101, payload, nullptr),
                  WorldIngressAdmissionResult::InvalidArgument);
        EXPECT_EQ(command, Unchanged);
    }
} // namespace psnr::world::tests
