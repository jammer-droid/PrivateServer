#include "pch.h"

#include "MovementInput.h"
#include "WorldMovementCommandAdmission.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace psnr::world::tests
{
    namespace
    {
        using MovementPayload = std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes>;

        void EncodeMovement(const std::uint32_t controlledEntityGeneration, const std::uint32_t targetServerTick,
                            const std::int16_t moveX, const std::int16_t moveY, MovementPayload* const outPayload)
        {
            ASSERT_NE(outPayload, nullptr);

            const protocol::v1::MovementInput input{controlledEntityGeneration, targetServerTick, moveX, moveY};
            ASSERT_EQ(protocol::v1::MovementInput::Encode(input, *outPayload), protocol::WorldProtocolError::Success);
        }

        void RegisterJoinedSession(WorldSessionRegistry* const registry, const WorldSessionKey sessionKey,
                                   const std::uint32_t playerId, const WorldEntityKey entityKey)
        {
            ASSERT_NE(registry, nullptr);
            ASSERT_TRUE(registry->TryRegister(sessionKey));
            ASSERT_TRUE(registry->TryBindPlayer(sessionKey, playerId, entityKey));
        }
    } // namespace

    static_assert(std::is_trivially_copyable_v<WorldMovementCommand>);

    TEST(WorldMovementCommandAdmissionTests, AcceptsOwnedMovementCommandAndNormalizesDiagonalInput)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr std::uint32_t PlayerId = 7;
        WorldMovementCommand command;

        RegisterJoinedSession(&registry, SessionKey, PlayerId, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};
        {
            MovementPayload payload;
            EncodeMovement(EntityKey.generation, 100, 32767, 32767, &payload);

            ASSERT_EQ(WorldMovementCommandAdmission::Admit(context, 100, payload, &command),
                      WorldIngressAdmissionResult::Accepted);
            payload.fill(std::byte{0});
        }

        EXPECT_EQ(command.sessionKey, SessionKey);
        EXPECT_EQ(command.playerId, PlayerId);
        EXPECT_EQ(command.entityKey, EntityKey);
        EXPECT_EQ(command.admittedServerTick, 100u);
        EXPECT_EQ(command.targetServerTick, 100u);
        EXPECT_NEAR(command.movementInputX, 0.70710677f, 0.000001f);
        EXPECT_NEAR(command.movementInputY, 0.70710677f, 0.000001f);
    }

    TEST(WorldMovementCommandAdmissionTests, RejectsMissingAndUnjoinedSessions)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey MissingSessionKey{10};
        constexpr WorldSessionKey UnjoinedSessionKey{11};
        MovementPayload payload;
        WorldMovementCommand command;
        EncodeMovement(1, 100, 0, 0, &payload);

        const WorldIngressAdmissionContext missingContext{registry, MissingSessionKey};
        EXPECT_EQ(WorldMovementCommandAdmission::Admit(missingContext, 100, payload, &command),
                  WorldIngressAdmissionResult::SessionNotFound);

        ASSERT_TRUE(registry.TryRegister(UnjoinedSessionKey));
        const WorldIngressAdmissionContext unjoinedContext{registry, UnjoinedSessionKey};
        EXPECT_EQ(WorldMovementCommandAdmission::Admit(unjoinedContext, 100, payload, &command),
                  WorldIngressAdmissionResult::SessionNotJoined);
    }

    TEST(WorldMovementCommandAdmissionTests, RejectsStaleControlledEntityGeneration)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey CurrentEntityKey{3, 2};
        MovementPayload payload;
        WorldMovementCommand command;
        RegisterJoinedSession(&registry, SessionKey, 7, CurrentEntityKey);
        EncodeMovement(1, 100, 0, 0, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        EXPECT_EQ(WorldMovementCommandAdmission::Admit(context, 100, payload, &command),
                  WorldIngressAdmissionResult::StaleEntityGeneration);
    }

    TEST(WorldMovementCommandAdmissionTests, ClassifiesLateMaximumFutureAndTooFarAheadTicks)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        MovementPayload payload;
        WorldMovementCommand command;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        EncodeMovement(EntityKey.generation, 99, 0, 0, &payload);
        EXPECT_EQ(
            WorldMovementCommandAdmission::Admit(context, WorldInboundMode::TargetServerTick, 100, payload, &command),
            WorldIngressAdmissionResult::LateTargetTick);

        EncodeMovement(EntityKey.generation, 108, 0, 0, &payload);
        EXPECT_EQ(
            WorldMovementCommandAdmission::Admit(context, WorldInboundMode::TargetServerTick, 100, payload, &command),
            WorldIngressAdmissionResult::Accepted);
        EXPECT_EQ(command.targetServerTick, 108u);

        EncodeMovement(EntityKey.generation, 109, 0, 0, &payload);
        EXPECT_EQ(
            WorldMovementCommandAdmission::Admit(context, WorldInboundMode::TargetServerTick, 100, payload, &command),
            WorldIngressAdmissionResult::TargetTickTooFarAhead);
    }

    TEST(WorldMovementCommandAdmissionTests, DoubleBufferedUsesReadEpochTickInsteadOfWireTargetTick)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        MovementPayload payload;
        WorldMovementCommand command;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        EncodeMovement(EntityKey.generation, 1, 0, 0, &payload);
        ASSERT_EQ(
            WorldMovementCommandAdmission::Admit(context, WorldInboundMode::DoubleBuffered, 100, payload, &command),
            WorldIngressAdmissionResult::Accepted);
        EXPECT_EQ(command.admittedServerTick, 100u);
        EXPECT_EQ(command.targetServerTick, 100u);

        EncodeMovement(EntityKey.generation, std::numeric_limits<std::uint32_t>::max(), 0, 0, &payload);
        ASSERT_EQ(
            WorldMovementCommandAdmission::Admit(context, WorldInboundMode::DoubleBuffered, 101, payload, &command),
            WorldIngressAdmissionResult::Accepted);
        EXPECT_EQ(command.admittedServerTick, 101u);
        EXPECT_EQ(command.targetServerTick, 101u);
    }

    TEST(WorldMovementCommandAdmissionTests, RejectsUnknownInboundModeWithoutChangingCommand)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr WorldMovementCommand Unchanged{
            SessionKey, 7, EntityKey, 100, 100, 0.5f, -0.5f,
        };
        MovementPayload payload;
        WorldMovementCommand command = Unchanged;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        EncodeMovement(EntityKey.generation, 100, 0, 0, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        EXPECT_EQ(
            WorldMovementCommandAdmission::Admit(context, static_cast<WorldInboundMode>(2), 100, payload, &command),
            WorldIngressAdmissionResult::InvalidArgument);
        EXPECT_EQ(command, Unchanged);
    }

    TEST(WorldMovementCommandAdmissionTests, RejectsMalformedAndNullOutputWithoutChangingCommand)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        const WorldMovementCommand unchanged{
            SessionKey, 7, EntityKey, 100, 100, 0.5f, -0.5f,
        };
        WorldMovementCommand command = unchanged;
        MovementPayload payload;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        EncodeMovement(EntityKey.generation, 100, 0, 0, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        const std::span<const std::byte> malformedPayload{payload.data(), payload.size() - 1};
        EXPECT_EQ(WorldMovementCommandAdmission::Admit(context, 100, malformedPayload, &command),
                  WorldIngressAdmissionResult::MalformedPayload);
        EXPECT_EQ(command, unchanged);

        EXPECT_EQ(WorldMovementCommandAdmission::Admit(context, 100, payload, nullptr),
                  WorldIngressAdmissionResult::InvalidArgument);
        EXPECT_EQ(command, unchanged);
    }
} // namespace psnr::world::tests
