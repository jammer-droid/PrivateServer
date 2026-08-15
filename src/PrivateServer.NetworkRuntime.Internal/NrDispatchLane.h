#pragma once

namespace psnr::core
{
    enum class NrDispatchLane
    {
        ServerIngress,
        SessionIngress,
        WorldIngress,

        Count,
    };

} // namespace psnr::core
