#pragma once

#include "WorldSessionRegistry.h"

#include <cstdint>

namespace psnr::world
{
    // Ingress 는 Runtime 에서 들어온 요청을 World 경계에서 즉시 해석하고 처리함
    // Command 는 특정 tick 에 적용되어 World 의 authoriative 상태를 변경할 Runtime 의 요청
    // Ingress 처리 이후에 Command 생성됨

    inline constexpr std::uint32_t MaxFutureInputTicks = 8; // 현재 server tick 기준 +8 까지 허용

    enum class WorldIngressAdmissionResult : std::uint8_t
    {
        Accepted = 0,
        InvalidArgument,
        MalformedPayload,
        SessionNotFound,
        SessionNotJoined,
        StaleEntityGeneration,
        LateTargetTick,
        TargetTickTooFarAhead,
    };

    struct WorldIngressAdmissionContext final
    {
        const WorldSessionRegistry& sessionRegistry;
        WorldSessionKey sessionKey{};
    };
} // namespace psnr::world
