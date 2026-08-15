#include "pch.h"

#include "NrSessionIoActor.h"

#include "NrErrorCode.h"
#include "NrSendIoContext.h"
#include "NrServerMetrics.h"
#include "NrSession.h"
#include "NrSessionIoOperations.h"

#include <cassert>
#include <cstdint>
#include <utility>

namespace psnr::core
{
    NrStatus NrSessionIoActor::HandleSendRequested(NrSessionSendRequestedEvent requested) noexcept
    {
        if (IsCloseRequested())
        {
            return NrStatus::Success();
        }

        NrStatus pushStatus = TryPushPendingSend(std::move(requested.payload));
        if (pushStatus.ErrorCode() == NrErrorCode::QueueFull)
        {
            RecordPendingSendQueueFull();
            return NrStatus::Success();
        }

        if (pushStatus.Failed())
        {
            return pushStatus;
        }

        return TryPostNextPendingSend();
    }

    NrStatus NrSessionIoActor::HandleSendCompleted(const NrSessionIoCompletedEvent& completed) noexcept
    {
        if (session_ == nullptr || ioOperations_ == nullptr)
        {
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const psnr::runtime::NrSessionKey sessionKey = session_->SessionKey();
        if (completed.sessionKey != sessionKey || !session_->HasPendingSend())
        {
            // Do not clear any current pending send here. A missing or foreign completion
            // violates the one-pending-send invariant and close/drain is the containment path.
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (completed.contextToken != session_->PendingSend().ContextToken())
        {
            // Token mismatch means the completion does not belong to the active send context.
            assert(completed.contextToken == session_->PendingSend().ContextToken());
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        RecordPendingSendIoCompleted();

        if (completed.status.Failed() || completed.bytesTransferred == 0)
        {
            // The session key and context token already matched the active pending send,
            // so this completion belongs to the current send context and the context can
            // be released. A failed or zero-byte send completion cannot make forward
            // progress; do not repost it because that could loop without sending bytes.
            session_->ClearPendingSend();
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Success();
        }

        psnr::runtime::NrSendIoContext& context = session_->PendingSend().Context();
        const NrStatus advanceStatus = context.AdvanceBytesSent(static_cast<std::uint32_t>(completed.bytesTransferred));
        if (advanceStatus.Failed())
        {
            session_->ClearPendingSend();
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return advanceStatus;
        }

        if (IsCloseRequested())
        {
            // Close/drain has already been requested. The matching completion is
            // cleanup-only now, so release the active context/ref instead of reposting
            // remaining bytes or starting the next queued send.
            session_->ClearPendingSend();
            return NrStatus::Success();
        }

        if (context.IsFullySent())
        {
            session_->ClearPendingSend();
            return TryPostNextPendingSend();
        }

        RecordPendingSendIoStarted();
        const NrStatus repostStatus = ioOperations_->RepostPendingSend(*session_);
        if (repostStatus.Failed())
        {
            RecordPendingSendIoCompleted();
            if (session_->HasPendingSend())
            {
                session_->ClearPendingSend();
            }

            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return repostStatus;
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionIoActor::TryPostNextPendingSend() noexcept
    {
        if (IsCloseRequested())
        {
            return NrStatus::Success();
        }

        if (session_ == nullptr || ioOperations_ == nullptr)
        {
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (!session_->IsValid())
        {
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (session_->HasPendingSend() || !HasPendingSend())
        {
            return NrStatus::Success();
        }

        NrResult<NrPayloadRef> payloadResult = TryPopPendingSend();
        if (payloadResult.Failed())
        {
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return payloadResult.Status();
        }

        RecordPendingSendIoStarted();
        const NrStatus postStatus = ioOperations_->PostSend(*session_, payloadResult.TakeValue());
        if (postStatus.Failed())
        {
            RecordPendingSendIoCompleted();
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return postStatus;
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionIoActor::TryPushPendingSend(NrPayloadRef payload) noexcept
    {
        if (pendingSendQueue_ == nullptr || pendingSendQueueCapacity_ == 0 || payload.IsEmpty())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (pendingSendQueueSize_ == pendingSendQueueCapacity_)
        {
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        const std::size_t tail = (pendingSendQueueHead_ + pendingSendQueueSize_) % pendingSendQueueCapacity_;
        pendingSendQueue_[tail] = std::move(payload);
        ++pendingSendQueueSize_;
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        recvDrainDependencies_.serverMetrics->RecordPendingSendQueued();
        return NrStatus::Success();
    }

    NrResult<NrPayloadRef> NrSessionIoActor::TryPopPendingSend() noexcept
    {
        if (pendingSendQueue_ == nullptr || pendingSendQueueCapacity_ == 0)
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::InvalidState);
        }

        if (!HasPendingSend())
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::QueueEmpty);
        }

        NrPayloadRef payload = std::move(pendingSendQueue_[pendingSendQueueHead_]);
        pendingSendQueueHead_ = (pendingSendQueueHead_ + 1) % pendingSendQueueCapacity_;
        --pendingSendQueueSize_;
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        recvDrainDependencies_.serverMetrics->RecordPendingSendDequeued();
        return NrResult<NrPayloadRef>(std::move(payload));
    }

    bool NrSessionIoActor::HasPendingSend() const noexcept
    {
        return pendingSendQueueSize_ != 0;
    }

    void NrSessionIoActor::RecordPendingSendQueueFull() noexcept
    {
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        drainReport_.pendingSendQueueFull = true;
        recvDrainDependencies_.serverMetrics->Record(
            psnr::runtime::NrPressureTransactionOutcome::SendAdmissionRejected);
        RecordCloseRequested(psnr::runtime::NrSessionEndReason::SendPressure);
    }
} // namespace psnr::core
