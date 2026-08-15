#pragma once

#include "NrResult.h"
#include "NrStatus.h"

#include "NrServerIoOpInternal.h"
#include "NrServerIoOpStateHandler.h"

namespace psnr::runtime
{
    using psnr::core::NrResult;
    using psnr::core::NrStatus;

    class NrServerIoOpControl final
    {
    public:
        NrServerIoOpControl() noexcept = default;

        [[nodiscard]] NrServerIoOpState CurrentState() const noexcept;

        [[nodiscard]] NrStatus TransitionTo(NrServerIoOpState nextState,
                                            INrServerIoOpStateHandler& nextHandler) noexcept;

        [[nodiscard]] NrResult<NrServerIoOpPollResult> Poll() noexcept;

    private:
        NrServerIoOpState state_ = NrServerIoOpState::Initializing;
        INrServerIoOpStateHandler* activeHandler_ = nullptr;
    };
} // namespace psnr::runtime
