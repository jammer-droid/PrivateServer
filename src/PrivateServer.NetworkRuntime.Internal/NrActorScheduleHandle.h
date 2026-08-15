#pragma once

#include "NrSessionIoEvent.h"
#include "NrSessionKey.h"
#include "NrStatus.h"

namespace psnr::runtime
{
    class NrSessionActorScheduler;

    class NrActorScheduleHandle final
    {
    public:
        NrActorScheduleHandle() noexcept = default;

        explicit NrActorScheduleHandle(NrSessionActorScheduler& scheduler) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] psnr::core::NrStatus Enqueue(psnr::core::NrSessionKey sessionKey,
                                                   psnr::core::NrSessionRecvEvent event) const noexcept;
        [[nodiscard]] psnr::core::NrStatus Enqueue(psnr::core::NrSessionKey sessionKey,
                                                   psnr::core::NrSessionSendEvent event) const noexcept;

    private:
        NrSessionActorScheduler* scheduler_ = nullptr; // non-owning
    };
} // namespace psnr::runtime
