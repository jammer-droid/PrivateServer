#include "pch.h"

#include "WorldControlCommandAdmission.h"

namespace psnr::world
{
    WorldTurnState WorldControlCommandAdmission::ToWorldTurnState(const protocol::v2::TurnState value) noexcept
    {
        switch (value)
        {
        case protocol::v2::TurnState::Straight:
            return WorldTurnState::Straight;
        case protocol::v2::TurnState::Left:
            return WorldTurnState::Left;
        case protocol::v2::TurnState::Right:
            return WorldTurnState::Right;
        default:
            return WorldTurnState::Straight;
        }
    }

    WorldBoostState WorldControlCommandAdmission::ToWorldBoostState(const protocol::v2::BoostState value) noexcept
    {
        switch (value)
        {
        case protocol::v2::BoostState::Off:
            return WorldBoostState::Off;
        case protocol::v2::BoostState::On:
            return WorldBoostState::On;
        default:
            return WorldBoostState::Off;
        }
    }

    WorldIngressAdmissionResult WorldControlCommandAdmission::Admit(const WorldIngressAdmissionContext& context,
                                                                    const std::uint32_t currentServerTick,
                                                                    const std::span<const std::byte> payload,
                                                                    WorldControlCommand* const outCommand) noexcept
    {
        if (outCommand == nullptr)
        {
            return WorldIngressAdmissionResult::InvalidArgument;
        }

        protocol::v2::ControlStateCommand controlState;
        if (protocol::v2::ControlStateCommand::Decode(payload, &controlState) != protocol::WorldProtocolError::Success)
        {
            return WorldIngressAdmissionResult::MalformedPayload;
        }

        WorldSession session;
        if (!context.sessionRegistry.TryFind(context.sessionKey, &session))
        {
            return WorldIngressAdmissionResult::SessionNotFound;
        }
        if (!session.IsJoined())
        {
            return WorldIngressAdmissionResult::SessionNotJoined;
        }
        if (controlState.controlledEntityGeneration != session.entityKey.generation)
        {
            return WorldIngressAdmissionResult::StaleEntityGeneration;
        }

        WorldControlCommand admittedCommand;
        admittedCommand.sessionKey = context.sessionKey;
        admittedCommand.playerId = session.playerId;   // sessionRegistry 값 사용
        admittedCommand.entityKey = session.entityKey; // sessionRegistry 값 사용
        admittedCommand.admittedServerTick = currentServerTick;
        admittedCommand.inputSequence = controlState.inputSequence;
        admittedCommand.turnState = ToWorldTurnState(controlState.turnState);
        admittedCommand.boostState = ToWorldBoostState(controlState.boostState);

        *outCommand = admittedCommand;
        return WorldIngressAdmissionResult::Accepted;
    }
} // namespace psnr::world
