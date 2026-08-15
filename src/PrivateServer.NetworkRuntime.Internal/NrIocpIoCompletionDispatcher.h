#pragma once

#include "NrIocpCompletionHandler.h"
#include "NrStatus.h"

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    class NrIoEventDispatcher;

    class NrIocpIoCompletionDispatcher final : public INrIocpCompletionHandler
    {
    public:
        explicit NrIocpIoCompletionDispatcher(NrIoEventDispatcher& dispatcher) noexcept;

        [[nodiscard]] NrStatus Dispatch(const NrIocpCompletionPacket& packet) noexcept;
        [[nodiscard]] NrStatus HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept override;
        [[nodiscard]] NrStatus HandleControlCompletion(const NrIocpCompletionPacket& packet) noexcept override;

    private:
        NrIoEventDispatcher& dispatcher_;
    };
} // namespace psnr::runtime
