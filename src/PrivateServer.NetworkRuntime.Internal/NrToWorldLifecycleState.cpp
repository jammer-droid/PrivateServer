#include "pch.h"

#include "NrToWorldLifecycleState.h"

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    NrStatus NrToWorldLifecycleState::RecordAccepted() noexcept
    {
        if (acceptedRecorded_) // 이미 accepted 상태
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        acceptedRecorded_ = true;
        return NrStatus::Success();
    }

    NrStatus NrToWorldLifecycleState::RecordClosed(const NrSessionEndReason reason) noexcept
    {
        if (reason == NrSessionEndReason::None)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!acceptedRecorded_ || closedRecorded_) // accepted 상태가 아닌데 close 요청 || 이미 closed 요청
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        endReason_ = reason;
        closedRecorded_ = true;
        return NrStatus::Success();
    }

    NrStatus NrToWorldLifecycleState::CommitNextPending() noexcept
    {
        switch (NextPendingKind())
        {
        case NrToWorldLifecycleNotificationKind::SessionAccepted:
            acceptedCommitted_ = true;
            return NrStatus::Success();
        case NrToWorldLifecycleNotificationKind::SessionClosed:
            closedCommitted_ = true;
            return NrStatus::Success();
        case NrToWorldLifecycleNotificationKind::None:
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    NrToWorldLifecycleNotificationKind NrToWorldLifecycleState::NextPendingKind() const noexcept
    {
        if (acceptedRecorded_ && !acceptedCommitted_)
        {
            return NrToWorldLifecycleNotificationKind::SessionAccepted;
        }

        if (acceptedCommitted_ && closedRecorded_ && !closedCommitted_)
        {
            return NrToWorldLifecycleNotificationKind::SessionClosed;
        }

        return NrToWorldLifecycleNotificationKind::None;
    }

    bool NrToWorldLifecycleState::CanPublishPacket() const noexcept
    {
        return acceptedCommitted_ && !closedRecorded_;
    }

    bool NrToWorldLifecycleState::IsSessionClosedPublished() const noexcept
    {
        return closedCommitted_;
    }

    NrStatus NrToWorldLifecycleState::GetEndReason(NrSessionEndReason& outReason) const noexcept
    {
        if (!closedRecorded_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        outReason = endReason_;
        return NrStatus::Success();
    }
} // namespace psnr::runtime::internal
