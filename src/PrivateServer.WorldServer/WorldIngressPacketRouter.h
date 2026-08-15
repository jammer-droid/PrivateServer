#pragma once

#include "WorldControlCommand.h"
#include "WorldIngressAdmission.h"
#include "WorldMovementCommandStore.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world
{
    enum class WorldIngressPacketRouteResult : std::uint8_t
    {
        MovementStored = 0,
        // Payload/session/generation 검증과 owned command 변환 완료. Entity state에는 아직 적용되지 않았다.
        ControlAdmitted,
        InvalidArgument,
        MalformedPayload,
        SessionNotFound,
        SessionNotJoined,
        StaleEntityGeneration,
        LateTargetTick,
        TargetTickTooFarAhead,
        DuplicateMovementInput,
        UnsupportedPacketType,
        UnknownPacketType,
    };

    // Runtime event에서 추출한 packet type과 borrowed payload를 World-owned 결과로 변환한다.
    // payload view는 Route 호출 안에서만 사용하며 control packet은 owned command로 caller에게 반환한다.
    class WorldIngressPacketRouter final
    {
    public:
        [[nodiscard]] static WorldIngressPacketRouteResult Route(
            const WorldIngressAdmissionContext& context, WorldInboundMode inboundMode, std::uint32_t currentServerTick,
            std::uint16_t packetType, std::span<const std::byte> payload,
            WorldMovementCommandStore& movementCommandStore,
            WorldControlCommand* outControlCommand = nullptr) noexcept;

        [[nodiscard]] static WorldIngressPacketRouteResult Route(
            const WorldIngressAdmissionContext& context, std::uint32_t currentServerTick, std::uint16_t packetType,
            std::span<const std::byte> payload, WorldMovementCommandStore& movementCommandStore,
            WorldControlCommand* outControlCommand = nullptr) noexcept;
    };
} // namespace psnr::world
