#pragma once

#include "NrIocpCompletionPacket.h"
#include "NrStatus.h"

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    class INrIocpCompletionHandler
    {
    public:
        INrIocpCompletionHandler() noexcept = default;

        INrIocpCompletionHandler(const INrIocpCompletionHandler&) = delete;
        INrIocpCompletionHandler& operator=(const INrIocpCompletionHandler&) = delete;

        virtual ~INrIocpCompletionHandler() noexcept = default;

        [[nodiscard]] virtual NrStatus HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept = 0;
        [[nodiscard]] virtual NrStatus HandleControlCompletion(const NrIocpCompletionPacket& packet) noexcept = 0;
    };
} // namespace psnr::runtime
