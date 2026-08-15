#include "pch.h"

#include "ControlStateCommand.h"
#include "MovementInput.h"
#include "WorldIngressPacketRouter.h"
#include "WorldPacketTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        using MovementPayload = std::array<std::byte, protocol::v1::MovementInput::Wire::PayloadBytes>;
        using ControlPayload = std::array<std::byte, protocol::v2::ControlStateCommand::Wire::PayloadBytes>;

        void EncodeMovement(const std::uint32_t controlledEntityGeneration, const std::uint32_t targetServerTick,
                            const std::int16_t moveX, const std::int16_t moveY, MovementPayload* const outPayload)
        {
            ASSERT_NE(outPayload, nullptr);

            const protocol::v1::MovementInput input{
                controlledEntityGeneration,
                targetServerTick,
                moveX,
                moveY,
            };
            ASSERT_EQ(protocol::v1::MovementInput::Encode(input, *outPayload), protocol::WorldProtocolError::Success);
        }

        void RegisterJoinedSession(WorldSessionRegistry* const registry, const WorldSessionKey sessionKey,
                                   const std::uint32_t playerId, const WorldEntityKey entityKey)
        {
            ASSERT_NE(registry, nullptr);
            ASSERT_TRUE(registry->TryRegister(sessionKey));
            ASSERT_TRUE(registry->TryBindPlayer(sessionKey, playerId, entityKey));
        }

        void EncodeControl(const std::uint32_t controlledEntityGeneration, const std::uint32_t inputSequence,
                           const protocol::v2::TurnState turnState, ControlPayload* const outPayload)
        {
            ASSERT_NE(outPayload, nullptr);
            const protocol::v2::ControlStateCommand input{
                controlledEntityGeneration,
                inputSequence,
                turnState,
                protocol::v2::BoostState::Off,
            };
            ASSERT_EQ(protocol::v2::ControlStateCommand::Encode(input, *outPayload),
                      protocol::WorldProtocolError::Success);
        }
    } // namespace

    TEST(WorldIngressPacketRouterTests, RoutesMovementInputIntoOwnedCommandStore)
    {
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr std::uint32_t PlayerId = 7;
        constexpr std::uint32_t ServerTick = 100;
        WorldSessionRegistry registry;
        WorldMovementCommandStore commandStore;
        MovementPayload payload;
        RegisterJoinedSession(&registry, SessionKey, PlayerId, EntityKey);
        EncodeMovement(EntityKey.generation, ServerTick, 16384, -16384, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        ASSERT_EQ(WorldIngressPacketRouter::Route(context, ServerTick,
                                                  static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput),
                                                  payload, commandStore),
                  WorldIngressPacketRouteResult::MovementStored);
        payload.fill(std::byte{0});

        std::vector<WorldMovementCommand> commands;
        ASSERT_TRUE(commandStore.TryTake(ServerTick, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0].sessionKey, SessionKey);
        EXPECT_EQ(commands[0].playerId, PlayerId);
        EXPECT_EQ(commands[0].entityKey, EntityKey);
        EXPECT_EQ(commands[0].targetServerTick, ServerTick);
        EXPECT_NEAR(commands[0].movementInputX, 16384.0f / 32767.0f, 0.000001f);
        EXPECT_NEAR(commands[0].movementInputY, -16384.0f / 32767.0f, 0.000001f);
    }

    TEST(WorldIngressPacketRouterTests, PreservesAdmissionRejectionWithoutStoringCommand)
    {
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry registry;
        WorldMovementCommandStore commandStore;
        MovementPayload payload;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        EncodeMovement(1, 100, 0, 0, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        EXPECT_EQ(WorldIngressPacketRouter::Route(context, 100,
                                                  static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput),
                                                  payload, commandStore),
                  WorldIngressPacketRouteResult::StaleEntityGeneration);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldIngressPacketRouterTests, RejectsDuplicateMovementInputForSameSessionAndTick)
    {
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry registry;
        WorldMovementCommandStore commandStore;
        MovementPayload payload;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        EncodeMovement(EntityKey.generation, 100, 0, 0, &payload);
        const WorldIngressAdmissionContext context{registry, SessionKey};
        constexpr std::uint16_t MovementPacketType = static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput);

        ASSERT_EQ(WorldIngressPacketRouter::Route(context, 100, MovementPacketType, payload, commandStore),
                  WorldIngressPacketRouteResult::MovementStored);
        EXPECT_EQ(WorldIngressPacketRouter::Route(context, 100, MovementPacketType, payload, commandStore),
                  WorldIngressPacketRouteResult::DuplicateMovementInput);
        EXPECT_EQ(commandStore.Size(), 1u);
    }

    TEST(WorldIngressPacketRouterTests, DoubleBufferedRoutesLatestInputToCurrentReadEpoch)
    {
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        WorldSessionRegistry registry;
        WorldMovementCommandStore commandStore;
        MovementPayload payload;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};
        constexpr std::uint16_t MovementPacketType = static_cast<std::uint16_t>(protocol::C2SPacketType::MovementInput);

        EncodeMovement(EntityKey.generation, 900, 1000, 0, &payload);
        ASSERT_EQ(WorldIngressPacketRouter::Route(context, WorldInboundMode::DoubleBuffered, 100, MovementPacketType,
                                                  payload, commandStore),
                  WorldIngressPacketRouteResult::MovementStored);
        EncodeMovement(EntityKey.generation, 901, -2000, 3000, &payload);
        ASSERT_EQ(WorldIngressPacketRouter::Route(context, WorldInboundMode::DoubleBuffered, 100, MovementPacketType,
                                                  payload, commandStore),
                  WorldIngressPacketRouteResult::MovementStored);

        std::vector<WorldMovementCommand> commands;
        ASSERT_TRUE(commandStore.TryTake(100, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0].targetServerTick, 100u);
        EXPECT_NEAR(commands[0].movementInputX, -2000.0f / 32767.0f, 0.000001f);
        EXPECT_NEAR(commands[0].movementInputY, 3000.0f / 32767.0f, 0.000001f);
    }

    TEST(WorldIngressPacketRouterTests, DistinguishesKnownUnsupportedAndUnknownPacketTypes)
    {
        WorldSessionRegistry registry;
        WorldMovementCommandStore commandStore;
        const WorldIngressAdmissionContext context{registry, WorldSessionKey{10}};

        EXPECT_EQ(WorldIngressPacketRouter::Route(
                      context, 100, static_cast<std::uint16_t>(protocol::C2SPacketType::WorldTimeSyncRequest), {},
                      commandStore),
                  WorldIngressPacketRouteResult::UnsupportedPacketType);
        EXPECT_EQ(WorldIngressPacketRouter::Route(context, 100, 0xFFFF, {}, commandStore),
                  WorldIngressPacketRouteResult::UnknownPacketType);
        EXPECT_EQ(commandStore.Size(), 0u);
    }

    TEST(WorldIngressPacketRouterTests, RoutesControlPacketIntoOwnedCommandForConsumer)
    {
        constexpr WorldSessionKey SessionKey{10};
        constexpr WorldEntityKey EntityKey{3, 2};
        constexpr std::uint16_t ControlPacketType =
            static_cast<std::uint16_t>(protocol::C2SPacketType::ControlStateCommand);
        WorldSessionRegistry registry;
        WorldMovementCommandStore movementStore;
        WorldControlCommand command;
        ControlPayload payload;
        RegisterJoinedSession(&registry, SessionKey, 7, EntityKey);
        const WorldIngressAdmissionContext context{registry, SessionKey};

        EncodeControl(EntityKey.generation, 10, protocol::v2::TurnState::Left, &payload);
        ASSERT_EQ(WorldIngressPacketRouter::Route(context, 100, ControlPacketType, payload, movementStore,
                                                  &command),
                  WorldIngressPacketRouteResult::ControlAdmitted);
        payload.fill(std::byte{0});

        EXPECT_EQ(command.sessionKey, SessionKey);
        EXPECT_EQ(command.playerId, 7u);
        EXPECT_EQ(command.entityKey, EntityKey);
        EXPECT_EQ(command.admittedServerTick, 100u);
        EXPECT_EQ(command.inputSequence, 10u);
        EXPECT_EQ(command.turnState, WorldTurnState::Left);

        EXPECT_EQ(WorldIngressPacketRouter::Route(context, 100, ControlPacketType, payload, movementStore),
                  WorldIngressPacketRouteResult::InvalidArgument);
    }
} // namespace psnr::world::tests
