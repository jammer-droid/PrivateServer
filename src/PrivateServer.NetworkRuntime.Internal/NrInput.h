#pragma once

#include "NrDispatchLane.h"
#include "NrSessionKey.h"
#include "NrPacketType.h"
#include "NrPayload.h"

#include <utility>

namespace psnr::core
{
    struct NrInput
    {
        NrInput() noexcept = default;

        NrInput(const NrInput&) = delete;
        NrInput& operator=(const NrInput&) = delete;

        NrInput(NrInput&& other) noexcept = default;
        NrInput& operator=(NrInput&& other) noexcept = default;
        ~NrInput() noexcept = default;

        NrInput(NrSessionKey inputSessionId, NrPacketType inputPacketType, NrDispatchLane inputDispatchLane,
                NrPayload&& inputPayload) noexcept
            : sessionId(inputSessionId)
            , packetType(inputPacketType)
            , dispatchLane(inputDispatchLane)
            , payload(std::move(inputPayload))
        {
        }

        NrSessionKey sessionId = 0;
        NrPacketType packetType{};
        NrDispatchLane dispatchLane = NrDispatchLane::Count;
        NrPayload payload;
    };

} // namespace psnr::core
