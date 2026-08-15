#include "pch.h"

#include "WorldIngressPacketRouter.h"

#include "WorldControlCommandAdmission.h"
#include "WorldMovementCommandAdmission.h"
#include "WorldPacketTypes.h"

namespace psnr::world
{
    namespace
    {
        [[nodiscard]] WorldIngressPacketRouteResult MapRejectedAdmissionResult(
            const WorldIngressAdmissionResult admissionResult) noexcept
        {
            switch (admissionResult)
            {
            case WorldIngressAdmissionResult::Accepted:
                return WorldIngressPacketRouteResult::InvalidArgument;
            case WorldIngressAdmissionResult::InvalidArgument:
                return WorldIngressPacketRouteResult::InvalidArgument;
            case WorldIngressAdmissionResult::MalformedPayload:
                return WorldIngressPacketRouteResult::MalformedPayload;
            case WorldIngressAdmissionResult::SessionNotFound:
                return WorldIngressPacketRouteResult::SessionNotFound;
            case WorldIngressAdmissionResult::SessionNotJoined:
                return WorldIngressPacketRouteResult::SessionNotJoined;
            case WorldIngressAdmissionResult::StaleEntityGeneration:
                return WorldIngressPacketRouteResult::StaleEntityGeneration;
            case WorldIngressAdmissionResult::LateTargetTick:
                return WorldIngressPacketRouteResult::LateTargetTick;
            case WorldIngressAdmissionResult::TargetTickTooFarAhead:
                return WorldIngressPacketRouteResult::TargetTickTooFarAhead;
            }

            return WorldIngressPacketRouteResult::InvalidArgument;
        }

        [[nodiscard]] WorldIngressPacketRouteResult RouteMovementInput(
            const WorldIngressAdmissionContext& context, const WorldInboundMode inboundMode,
            const std::uint32_t currentServerTick, const std::span<const std::byte> payload,
            WorldMovementCommandStore& movementCommandStore) noexcept
        {
            WorldMovementCommand command;
            const WorldIngressAdmissionResult admissionResult =
                WorldMovementCommandAdmission::Admit(context, inboundMode, currentServerTick, payload, &command);
            if (admissionResult != WorldIngressAdmissionResult::Accepted)
            {
                return MapRejectedAdmissionResult(admissionResult);
            }

            const WorldMovementCommandStoreResult storeResult = movementCommandStore.TryStore(inboundMode, command);
            switch (storeResult)
            {
            case WorldMovementCommandStoreResult::Stored:
            case WorldMovementCommandStoreResult::Replaced:
                return WorldIngressPacketRouteResult::MovementStored;
            case WorldMovementCommandStoreResult::InvalidCommand:
                return WorldIngressPacketRouteResult::InvalidArgument;
            case WorldMovementCommandStoreResult::DuplicateTargetTick:
                return WorldIngressPacketRouteResult::DuplicateMovementInput;
            }

            return WorldIngressPacketRouteResult::InvalidArgument;
        }

        [[nodiscard]] WorldIngressPacketRouteResult RouteControlStateCommand(
            const WorldIngressAdmissionContext& context, const std::uint32_t currentServerTick,
            const std::span<const std::byte> payload, WorldControlCommand* const outControlCommand) noexcept
        {
            if (outControlCommand == nullptr)
            {
                return WorldIngressPacketRouteResult::InvalidArgument;
            }

            const WorldIngressAdmissionResult admissionResult =
                WorldControlCommandAdmission::Admit(context, currentServerTick, payload, outControlCommand);
            if (admissionResult != WorldIngressAdmissionResult::Accepted)
            {
                return MapRejectedAdmissionResult(admissionResult);
            }
            return WorldIngressPacketRouteResult::ControlAdmitted;
        }
    } // namespace

    WorldIngressPacketRouteResult WorldIngressPacketRouter::Route(
        const WorldIngressAdmissionContext& context, const WorldInboundMode inboundMode,
        const std::uint32_t currentServerTick, const std::uint16_t packetType, const std::span<const std::byte> payload,
        WorldMovementCommandStore& movementCommandStore, WorldControlCommand* const outControlCommand) noexcept
    {
        switch (static_cast<protocol::C2SPacketType>(packetType))
        {
        case protocol::C2SPacketType::MovementInput:
            return RouteMovementInput(context, inboundMode, currentServerTick, payload, movementCommandStore);
        case protocol::C2SPacketType::ControlStateCommand:
            return RouteControlStateCommand(context, currentServerTick, payload, outControlCommand);
        case protocol::C2SPacketType::JoinWorldRequest:
        case protocol::C2SPacketType::ObserveWorldRequest:
        case protocol::C2SPacketType::WorldTimeSyncRequest:
            return WorldIngressPacketRouteResult::UnsupportedPacketType;
        }

        return WorldIngressPacketRouteResult::UnknownPacketType;
    }

    WorldIngressPacketRouteResult WorldIngressPacketRouter::Route(const WorldIngressAdmissionContext& context,
                                                                  const std::uint32_t currentServerTick,
                                                                  const std::uint16_t packetType,
                                                                  const std::span<const std::byte> payload,
                                                                  WorldMovementCommandStore& movementCommandStore,
                                                                  WorldControlCommand* const outControlCommand) noexcept
    {
        return Route(context, WorldInboundMode::TargetServerTick, currentServerTick, packetType, payload,
                     movementCommandStore, outControlCommand);
    }
} // namespace psnr::world
