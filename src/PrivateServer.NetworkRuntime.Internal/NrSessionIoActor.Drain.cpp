#include "pch.h"

#include "NrSessionIoActor.h"

#include "NrErrorCode.h"
#include "NrServerMetrics.h"

#include <utility>

namespace psnr::core
{
    NrResult<NrActorDrainReport> NrSessionIoActor::Drain(NrActorDrainBudget budget) noexcept
    {
        if (budget.maxEvents == 0)
        {
            return NrResult<NrActorDrainReport>::Failure(NrErrorCode::InvalidArgument);
        }

        if (acceptRecvMailbox_ == nullptr || !sendChannelControl_.IsValid())
        {
            return NrResult<NrActorDrainReport>::Failure(NrErrorCode::InvalidState);
        }

        NrActorDrainReport report;
        while (report.drainedCount < budget.maxEvents)
        {
            NrResult<bool> acceptRecvDrainedResult = TryDrainOneAcceptRecvEvent();
            if (acceptRecvDrainedResult.Failed())
            {
                return NrResult<NrActorDrainReport>::Failure(acceptRecvDrainedResult.Status());
            }

            const bool acceptRecvDrained = acceptRecvDrainedResult.Value();
            if (acceptRecvDrained)
            {
                ++report.drainedCount;
                if (report.drainedCount == budget.maxEvents)
                {
                    break;
                }
            }

            NrResult<bool> sendDrainedResult = TryDrainOneSendEvent();
            if (sendDrainedResult.Failed())
            {
                return NrResult<NrActorDrainReport>::Failure(sendDrainedResult.Status());
            }

            const bool sendDrained = sendDrainedResult.Value();
            if (sendDrained)
            {
                ++report.drainedCount;
            }

            if (!acceptRecvDrained && !sendDrained)
            {
                return NrResult<NrActorDrainReport>(report);
            }
        }

        report.needsReschedule = (report.drainedCount == budget.maxEvents);
        return NrResult<NrActorDrainReport>(report);
    }

    NrResult<bool> NrSessionIoActor::TryDrainOneAcceptRecvEvent() noexcept
    {
        NrSessionRecvEvent event;
        const NrStatus dequeueStatus = acceptRecvMailbox_->TryDequeue(event);
        if (dequeueStatus.ErrorCode() == NrErrorCode::QueueEmpty)
        {
            return NrResult<bool>(false);
        }

        if (dequeueStatus.Failed())
        {
            return NrResult<bool>::Failure(dequeueStatus);
        }

        switch (event.type)
        {
        case NrSessionRecvEventType::Accepted:
        {
            const NrStatus acceptedStatus = HandleAccepted();
            if (acceptedStatus.Failed())
            {
                return NrResult<bool>::Failure(acceptedStatus);
            }

            break;
        }
        case NrSessionRecvEventType::RecvCompleted:
        {
            const NrStatus recvCompletedStatus = HandleRecvCompleted(event.completed);
            if (recvCompletedStatus.Failed())
            {
                return NrResult<bool>::Failure(recvCompletedStatus);
            }

            break;
        }
        case NrSessionRecvEventType::CloseRequested:
        {
            if (event.endReason == psnr::runtime::NrSessionEndReason::None)
            {
                return NrResult<bool>::Failure(NrErrorCode::InvalidArgument);
            }

            RecordCloseRequested(event.endReason);
            break;
        }
        }

        return NrResult<bool>(true);
    }

    NrResult<bool> NrSessionIoActor::TryDrainOneSendEvent() noexcept
    {
        NrSessionSendEvent event;
        if (sendMailbox_ == nullptr)
        {
            return NrResult<bool>::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus dequeueStatus = sendMailbox_->TryDequeue(event);
        if (dequeueStatus.ErrorCode() == NrErrorCode::QueueEmpty)
        {
            return NrResult<bool>(false);
        }

        if (dequeueStatus.Failed())
        {
            return NrResult<bool>::Failure(dequeueStatus);
        }

        if (event.type == NrSessionSendEventType::SendRequested)
        {
            assert(recvDrainDependencies_.serverMetrics != nullptr);
            recvDrainDependencies_.serverMetrics->RecordSendMailboxDequeued();
        }

        switch (event.type)
        {
        case NrSessionSendEventType::SendRequested:
        {
            const NrStatus sendRequestedStatus = HandleSendRequested(std::move(event.requested));
            if (sendRequestedStatus.Failed())
            {
                return NrResult<bool>::Failure(sendRequestedStatus);
            }

            break;
        }
        case NrSessionSendEventType::SendCompleted:
        {
            const NrStatus sendCompletedStatus = HandleSendCompleted(event.completed);
            if (sendCompletedStatus.Failed())
            {
                return NrResult<bool>::Failure(sendCompletedStatus);
            }

            break;
        }
        }

        return NrResult<bool>(true);
    }
} // namespace psnr::core
