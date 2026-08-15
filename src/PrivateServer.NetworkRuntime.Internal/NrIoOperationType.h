#pragma once

namespace psnr::runtime
{
    enum class NrIoOperationType
    {
        Unknown,
        Accept,
        Recv,
        Send,
        Connect,
    };
} // namespace psnr::runtime
