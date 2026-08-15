#include "pch.h"

#include "NrIocpCompletionPump.h"

namespace psnr::runtime
{
    NrIocpCompletionPump::NrIocpCompletionPump(NrIocpPort& port, INrIocpCompletionHandler& handler) noexcept
        : port_(port)
        , handler_(handler)
    {
    }

    NrStatus NrIocpCompletionPump::PumpOnce() noexcept
    {
        NrIocpCompletionPacket packet;
        const NrStatus waitStatus = port_.WaitForCompletion(packet);
        if (waitStatus.Failed())
        {
            return waitStatus;
        }

        if (packet.overlapped == nullptr)
        {
            return handler_.HandleControlCompletion(packet);
        }

        return handler_.HandleIoCompletion(packet);
    }
} // namespace psnr::runtime
