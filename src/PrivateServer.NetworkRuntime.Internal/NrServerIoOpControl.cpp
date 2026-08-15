#include "pch.h"

#include "NrServerIoOpControl.h"

#include "NrErrorCode.h"

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrServerIoOpState NrServerIoOpControl::CurrentState() const noexcept
    {
        return state_;
    }

    NrStatus NrServerIoOpControl::TransitionTo(NrServerIoOpState nextState,
                                               INrServerIoOpStateHandler& nextHandler) noexcept
    {
        if (activeHandler_ != nullptr)
        {
            const NrStatus exitStatus = activeHandler_->Exit();
            if (exitStatus.Failed())
            {
                return exitStatus;
            }

            activeHandler_ = nullptr;
        }

        const NrStatus enterStatus = nextHandler.Enter();
        if (enterStatus.Failed())
        {
            return enterStatus;
        }

        state_ = nextState;
        activeHandler_ = &nextHandler;
        return NrStatus::Success();
    }

    NrResult<NrServerIoOpPollResult> NrServerIoOpControl::Poll() noexcept
    {
        if (activeHandler_ == nullptr)
        {
            return NrResult<NrServerIoOpPollResult>::Failure(NrErrorCode::InvalidState);
        }

        return activeHandler_->Poll();
    }
} // namespace psnr::runtime
