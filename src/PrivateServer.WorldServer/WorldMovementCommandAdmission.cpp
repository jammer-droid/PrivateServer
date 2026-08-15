#include "pch.h"

#include "WorldMovementCommandAdmission.h"

#include "MovementInput.h"

#include <cmath>
#include <limits>

namespace psnr::world
{
    WorldIngressAdmissionResult WorldMovementCommandAdmission::Admit(const WorldIngressAdmissionContext& context,
                                                                     const WorldInboundMode inboundMode,
                                                                     const std::uint32_t currentServerTick,
                                                                     const std::span<const std::byte> payload,
                                                                     WorldMovementCommand* const outCommand) noexcept
    {
        if (outCommand == nullptr)
        {
            return WorldIngressAdmissionResult::InvalidArgument;
        }
        if (inboundMode != WorldInboundMode::TargetServerTick && inboundMode != WorldInboundMode::DoubleBuffered)
        {
            return WorldIngressAdmissionResult::InvalidArgument;
        }

        protocol::v1::MovementInput movementInput;
        // MovementInput 을 즉시 decode
        if (protocol::v1::MovementInput::Decode(payload, &movementInput) != protocol::WorldProtocolError::Success)
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
        if (movementInput.controlledEntityGeneration != session.entityKey.generation)
        {
            return WorldIngressAdmissionResult::StaleEntityGeneration;
        }

        std::uint32_t admittedTargetServerTick = currentServerTick;
        if (inboundMode == WorldInboundMode::TargetServerTick)
        {
            if (movementInput.targetServerTick < currentServerTick)
            {
                return WorldIngressAdmissionResult::LateTargetTick;
            }

            const std::uint32_t maximumTargetTick =
                currentServerTick > std::numeric_limits<std::uint32_t>::max() - MaxFutureInputTicks
                    ? std::numeric_limits<std::uint32_t>::max()
                    : currentServerTick + MaxFutureInputTicks;
            if (movementInput.targetServerTick > maximumTargetTick)
            {
                return WorldIngressAdmissionResult::TargetTickTooFarAhead;
            }

            admittedTargetServerTick = movementInput.targetServerTick;
        }

        // x, y 입력을 [-1.0, 1.0] 으로 정규화
        float movementInputX = static_cast<float>(movementInput.moveX) / 32767.0f;
        float movementInputY = static_cast<float>(movementInput.moveY) / 32767.0f;
        const float lengthSquared = movementInputX * movementInputX + movementInputY * movementInputY;
        if (lengthSquared > 1.0f) // 대각선 입력 길이가 1.0 을 넘지 않도록 vector 정규화
        {
            const float inverseLength = 1.0f / std::sqrt(lengthSquared); // 실제 길이 계산
            movementInputX *= inverseLength;
            movementInputY *= inverseLength;
        }

        WorldMovementCommand admittedCommand;
        admittedCommand.sessionKey = context.sessionKey;
        admittedCommand.playerId = session.playerId;
        admittedCommand.entityKey = session.entityKey;
        admittedCommand.admittedServerTick = currentServerTick;
        admittedCommand.targetServerTick = admittedTargetServerTick;
        admittedCommand.movementInputX = movementInputX;
        admittedCommand.movementInputY = movementInputY;

        *outCommand = admittedCommand;
        return WorldIngressAdmissionResult::Accepted;
    }

    WorldIngressAdmissionResult WorldMovementCommandAdmission::Admit(const WorldIngressAdmissionContext& context,
                                                                     const std::uint32_t currentServerTick,
                                                                     const std::span<const std::byte> payload,
                                                                     WorldMovementCommand* const outCommand) noexcept
    {
        return Admit(context, WorldInboundMode::TargetServerTick, currentServerTick, payload, outCommand);
    }
} // namespace psnr::world
