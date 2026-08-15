#include "pch.h"

#include "NrSessionSendChannelControl.h"

#include "NrErrorCode.h"
#include "NrSessionIoEvent.h"

#include <new>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrPayloadRef;
    using psnr::core::NrResult;
    using psnr::core::NrSessionKey;
    using psnr::core::NrSessionSendEvent;
    using psnr::core::NrSessionSendEventType;
    using psnr::core::NrStatus;

    NrResult<NrSessionSendChannelControl*> NrSessionSendChannelControl::Create(
        NrSessionKey sessionKey, const NrActorScheduleHandle& scheduleHandle,
        internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept
    {
        if (sessionKey == 0 || !scheduleHandle.IsValid() || !submissionAdmission.IsValid())
        {
            return NrResult<NrSessionSendChannelControl*>::Failure(NrErrorCode::InvalidArgument);
        }

        NrSessionSendChannelControl* control = new (std::nothrow)
            NrSessionSendChannelControl(sessionKey, scheduleHandle, std::move(submissionAdmission));
        if (control == nullptr)
        {
            return NrResult<NrSessionSendChannelControl*>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<NrSessionSendChannelControl*>(control);
    }

    void NrSessionSendChannelControl::AddRef() noexcept
    {
        refCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void NrSessionSendChannelControl::ReleaseRef() noexcept
    {
        if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete this;
        }
    }

    bool NrSessionSendChannelControl::IsOpen() const noexcept
    {
        return state_.load(std::memory_order_acquire) == NrState::Open;
    }

    void NrSessionSendChannelControl::MarkClosed() noexcept
    {
        state_.store(NrState::Closed, std::memory_order_release);
    }

    NrStatus NrSessionSendChannelControl::EnqueueSendRequested(
        NrPayloadRef payload, const internal::NrSubmissionPermit& permit) const noexcept
    {
        if (!scheduleHandle_.IsValid() || !submissionAdmission_.MatchesPermit(permit))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (!IsOpen())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (payload.IsEmpty())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrSessionSendEvent event{NrSessionSendEventType::SendRequested, {std::move(payload)}, {}};
        return scheduleHandle_.Enqueue(sessionKey_, std::move(event));
    }

    NrSessionSendChannelControl::NrSessionSendChannelControl(
        NrSessionKey ownerSessionKey, NrActorScheduleHandle ownerScheduleHandle,
        internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept
        : sessionKey_(ownerSessionKey)
        , scheduleHandle_(ownerScheduleHandle)
        , submissionAdmission_(std::move(submissionAdmission))
    {
    }

    NrSessionSendChannelControl::~NrSessionSendChannelControl() noexcept = default;

    NrSessionSendChannelControlHandle::NrSessionSendChannelControlHandle(
        NrSessionSendChannelControlHandle&& other) noexcept
        : control_(std::exchange(other.control_, nullptr))
    {
    }

    NrSessionSendChannelControlHandle& NrSessionSendChannelControlHandle::operator=(
        NrSessionSendChannelControlHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            control_ = std::exchange(other.control_, nullptr);
        }

        return *this;
    }

    NrSessionSendChannelControlHandle::~NrSessionSendChannelControlHandle() noexcept
    {
        Reset();
    }

    NrResult<NrSessionSendChannelControlHandle> NrSessionSendChannelControlHandle::Create(
        NrSessionKey sessionKey, const NrActorScheduleHandle& scheduleHandle,
        internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept
    {
        NrResult<NrSessionSendChannelControl*> controlResult = NrSessionSendChannelControl::Create(
            sessionKey, scheduleHandle, std::move(submissionAdmission));
        if (controlResult.Failed())
        {
            return NrResult<NrSessionSendChannelControlHandle>::Failure(controlResult.Status());
        }

        return NrResult<NrSessionSendChannelControlHandle>(
            NrSessionSendChannelControlHandle(controlResult.TakeValue()));
    }

    NrSessionSendChannelControlHandle NrSessionSendChannelControlHandle::Retain(
        NrSessionSendChannelControl& control) noexcept
    {
        control.AddRef();
        return NrSessionSendChannelControlHandle(&control);
    }

    bool NrSessionSendChannelControlHandle::IsValid() const noexcept
    {
        return control_ != nullptr;
    }

    bool NrSessionSendChannelControlHandle::IsOpen() const noexcept
    {
        return control_ != nullptr && control_->IsOpen();
    }

    void NrSessionSendChannelControlHandle::MarkClosed() noexcept
    {
        if (control_ != nullptr)
        {
            control_->MarkClosed();
        }
    }

    NrSessionSendChannelControl* NrSessionSendChannelControlHandle::Get() const noexcept
    {
        return control_;
    }

    NrSessionSendChannelControlHandle::NrSessionSendChannelControlHandle(NrSessionSendChannelControl* control) noexcept
        : control_(control)
    {
    }

    void NrSessionSendChannelControlHandle::Reset() noexcept
    {
        NrSessionSendChannelControl* control = std::exchange(control_, nullptr);
        if (control != nullptr)
        {
            control->ReleaseRef();
        }
    }
} // namespace psnr::runtime
