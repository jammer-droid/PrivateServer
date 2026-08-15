#pragma once

#include "NrDispatchLane.h"
#include "NrPacketType.h"

namespace psnr::core
{
    struct NrPacketDispatchRule
    {
        NrPacketType packetType{};
        NrDispatchLane dispatchLane = NrDispatchLane::Count;
    };

} // namespace psnr::core
