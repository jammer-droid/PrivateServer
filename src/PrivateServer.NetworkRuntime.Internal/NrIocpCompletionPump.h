#pragma once

#include "NrIocpCompletionHandler.h"
#include "NrIocpPort.h"
#include "NrStatus.h"

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    class NrIocpCompletionPump final
    {
    public:
        NrIocpCompletionPump(NrIocpPort& port, INrIocpCompletionHandler& handler) noexcept;

        [[nodiscard]] NrStatus PumpOnce() noexcept;

    private:
        NrIocpPort& port_;
        INrIocpCompletionHandler& handler_;
    };
} // namespace psnr::runtime
