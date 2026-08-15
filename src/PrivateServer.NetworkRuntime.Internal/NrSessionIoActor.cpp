#include "pch.h"

#include "NrSessionIoActor.h"

#include "NrErrorCode.h"
#include "NrSession.h"
#include "NrSessionActorScheduler.h"
#include "NrServerMetrics.h"
#include "NrToWorldHandoff.h"

#include <cassert>
#include <new>
#include <utility>

namespace psnr::core
{
    NrResult<std::unique_ptr<NrSessionIoActor>> NrSessionIoActor::Create(
        NrMemoryPoolManager& memoryPoolManager, std::size_t mailboxCapacity, std::size_t pendingSendQueueCapacity,
        std::unique_ptr<psnr::runtime::NrSession> session, psnr::runtime::NrSessionIoOperations& ioOperations,
        psnr::runtime::NrActorScheduleHandle actorScheduleHandle,
        psnr::runtime::internal::NrSubmissionAdmissionHandle submissionAdmission,
        NrSessionRecvDrainDependencies recvDrainDependencies) noexcept
    {
        if (session == nullptr || mailboxCapacity == 0 || pendingSendQueueCapacity == 0 ||
            !actorScheduleHandle.IsValid() || !recvDrainDependencies.IsValid())
        {
            return NrResult<std::unique_ptr<NrSessionIoActor>>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<std::unique_ptr<NrAcceptRecvMailbox>> acceptRecvMailboxResult = NrAcceptRecvMailbox::Create(
            memoryPoolManager, NrMemoryPoolRole::SessionAcceptRecvMailboxStorage, mailboxCapacity);
        if (acceptRecvMailboxResult.Failed())
        {
            return NrResult<std::unique_ptr<NrSessionIoActor>>::Failure(acceptRecvMailboxResult.Status());
        }

        NrResult<std::unique_ptr<NrSendMailbox>> sendMailboxResult =
            NrSendMailbox::Create(memoryPoolManager, NrMemoryPoolRole::SessionSendMailboxStorage, mailboxCapacity);
        if (sendMailboxResult.Failed())
        {
            return NrResult<std::unique_ptr<NrSessionIoActor>>::Failure(sendMailboxResult.Status());
        }

        NrResult<psnr::runtime::NrSessionSendChannelControlHandle> sendChannelControlResult =
            psnr::runtime::NrSessionSendChannelControlHandle::Create(session->SessionKey(), actorScheduleHandle,
                                                                     std::move(submissionAdmission));
        if (sendChannelControlResult.Failed())
        {
            return NrResult<std::unique_ptr<NrSessionIoActor>>::Failure(sendChannelControlResult.Status());
        }

        std::unique_ptr<NrPayloadRef[]> pendingSendQueue(new (std::nothrow) NrPayloadRef[pendingSendQueueCapacity]);
        if (pendingSendQueue == nullptr)
        {
            return NrResult<std::unique_ptr<NrSessionIoActor>>::Failure(NrErrorCode::OutOfMemory);
        }

        std::unique_ptr<NrSessionIoActor> actor(new (std::nothrow) NrSessionIoActor(
            acceptRecvMailboxResult.TakeValue(), sendMailboxResult.TakeValue(), sendChannelControlResult.TakeValue(),
            std::move(pendingSendQueue), pendingSendQueueCapacity, std::move(session), ioOperations,
            recvDrainDependencies));
        if (actor == nullptr)
        {
            return NrResult<std::unique_ptr<NrSessionIoActor>>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<std::unique_ptr<NrSessionIoActor>>(std::move(actor));
    }

    NrSessionIoActor::~NrSessionIoActor() noexcept
    {
        if (recvDrainDependencies_.serverMetrics == nullptr)
        {
            assert(pendingRecvIoCount_ == 0);
            assert(pendingSendIoCount_ == 0);
            return;
        }

        while (pendingRecvIoCount_ > 0)
        {
            recvDrainDependencies_.serverMetrics->RecordPendingRecvIoCompleted();
            --pendingRecvIoCount_;
        }
        while (pendingSendIoCount_ > 0)
        {
            recvDrainDependencies_.serverMetrics->RecordPendingSendIoCompleted();
            --pendingSendIoCount_;
        }

        // actor가 제거될 때 mailbox나 actor-local queue에 남은 payload도 함께 파기된다.
        // 전역 backlog depth가 종료 후 남지 않도록 아직 소비되지 않은 항목을 같은 수만큼 정리한다.
        if (sendMailbox_ != nullptr)
        {
            NrSessionSendEvent event;
            while (sendMailbox_->TryDequeue(event).Succeeded())
            {
                if (event.type == NrSessionSendEventType::SendRequested)
                {
                    recvDrainDependencies_.serverMetrics->RecordSendMailboxDequeued();
                }
                event = NrSessionSendEvent{};
            }
        }
        while (pendingSendQueueSize_ > 0)
        {
            recvDrainDependencies_.serverMetrics->RecordPendingSendDequeued();
            --pendingSendQueueSize_;
        }
    }

    NrSessionIoActor::NrSessionIoActor(std::unique_ptr<NrAcceptRecvMailbox> acceptRecvMailbox,
                                       std::unique_ptr<NrSendMailbox> sendMailbox,
                                       psnr::runtime::NrSessionSendChannelControlHandle sendChannelControl,
                                       std::unique_ptr<NrPayloadRef[]> pendingSendQueue,
                                       std::size_t pendingSendQueueCapacity,
                                       std::unique_ptr<psnr::runtime::NrSession> session,
                                       psnr::runtime::NrSessionIoOperations& ioOperations,
                                       NrSessionRecvDrainDependencies recvDrainDependencies) noexcept
        : acceptRecvMailbox_(std::move(acceptRecvMailbox))
        , sendMailbox_(std::move(sendMailbox))
        , sendChannelControl_(std::move(sendChannelControl))
        , pendingSendQueue_(std::move(pendingSendQueue))
        , pendingSendQueueCapacity_(pendingSendQueueCapacity)
        , session_(std::move(session))
        , ioOperations_(&ioOperations)
        , recvDrainDependencies_(recvDrainDependencies)
    {
    }

    NrSessionIoActor::NrMailboxHandle NrSessionIoActor::MailboxHandle(NrActorScheduleGate& scheduleGate) const noexcept
    {
        return NrMailboxHandle(*acceptRecvMailbox_, scheduleGate);
    }

    NrSessionIoActor::NrSendMailboxHandle NrSessionIoActor::SendMailboxHandle() const noexcept
    {
        return sendMailbox_ == nullptr ? NrSendMailboxHandle() : NrSendMailboxHandle(*sendMailbox_, scheduleGate_);
    }

    NrActorScheduleGate* NrSessionIoActor::ScheduleGate() const noexcept
    {
        return &scheduleGate_;
    }

    NrSessionIoDrainReport NrSessionIoActor::TakeDrainReport() noexcept
    {
        RefreshCloseReady();
        return std::exchange(drainReport_, {});
    }

    bool NrSessionIoActor::HasPendingIo() const noexcept
    {
        return pendingRecvIoCount_ != 0 || pendingSendIoCount_ != 0;
    }

    void NrSessionIoActor::RecordPendingRecvIoStarted() noexcept
    {
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        assert(pendingRecvIoCount_ == 0);
        ++pendingRecvIoCount_;
        recvDrainDependencies_.serverMetrics->RecordPendingRecvIoStarted();
        RefreshCloseReady();
    }

    void NrSessionIoActor::RecordPendingRecvIoCompleted() noexcept
    {
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        assert(pendingRecvIoCount_ != 0);

        --pendingRecvIoCount_;
        recvDrainDependencies_.serverMetrics->RecordPendingRecvIoCompleted();
        RefreshCloseReady();
    }

    void NrSessionIoActor::RecordPendingSendIoStarted() noexcept
    {
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        assert(pendingSendIoCount_ == 0);
        ++pendingSendIoCount_;
        recvDrainDependencies_.serverMetrics->RecordPendingSendIoStarted();
        RefreshCloseReady();
    }

    void NrSessionIoActor::RecordPendingSendIoCompleted() noexcept
    {
        assert(recvDrainDependencies_.serverMetrics != nullptr);
        assert(pendingSendIoCount_ != 0);

        --pendingSendIoCount_;
        recvDrainDependencies_.serverMetrics->RecordPendingSendIoCompleted();
        RefreshCloseReady();
    }

    void NrSessionIoActor::RecordCloseRequested(const psnr::runtime::NrSessionEndReason endReason) noexcept
    {
        assert(endReason != psnr::runtime::NrSessionEndReason::None);

        if (!closeRequested_)
        {
            closeRequested_ = true;
            endReason_ = endReason;

            // Close 요청 이후 새 recv를 게시하지 않더라도 이미 pending인 recv는 peer 입력 없이는 완료되지 않는다.
            // actor가 소유한 socket을 닫아 pending IO completion을 깨우고 close/drain을 끝낼 수 있게 한다.
            if (session_ != nullptr)
            {
                static_cast<void>(session_->Socket().Close());
            }

            switch (endReason_)
            {
            case psnr::runtime::NrSessionEndReason::ReceivePressure:
                recvDrainDependencies_.serverMetrics->Record(
                    psnr::runtime::NrPressureTransactionOutcome::ReceivePressureCloseCommitted);
                break;
            case psnr::runtime::NrSessionEndReason::SendPressure:
                recvDrainDependencies_.serverMetrics->Record(
                    psnr::runtime::NrPressureTransactionOutcome::SendPressureCloseCommitted);
                break;
            default:
                break;
            }

            if (sendChannelControl_.IsOpen())
            {
                sendChannelControl_.MarkClosed();

                if (session_ != nullptr && recvDrainDependencies_.toWorldHandoff != nullptr)
                {
                    const NrStatus publicationStatus =
                        recvDrainDependencies_.toWorldHandoff->RecordClosed(session_->SessionKey(), endReason_);
                    if (publicationStatus.Failed())
                    {
                        // Handoff는 queue/promotion이 지연돼도 fact가 slot/pending에 보존되면 success를 반환한다.
                        // 따라서 여기까지 온 failure는 Reserve -> Accepted -> Closed 순서가 깨진 구현 오류다.
                        assert(publicationStatus.Succeeded());
                    }
                }
            }
        }

        RefreshCloseReady();
    }

    void NrSessionIoActor::RecordServerStopping() noexcept
    {
        RecordCloseRequested(psnr::runtime::NrSessionEndReason::ServerStopping);
    }

    void NrSessionIoActor::RecordPostRecvRequested() noexcept
    {
        drainReport_.requestPostRecv = true;
    }

    void NrSessionIoActor::RefreshCloseReady() noexcept
    {
        // close 요청이 들어왔고(closeRequested == true), pending recv/send IO가 모두 0
        // actor close ready 상태
        drainReport_.endReason = endReason_;
        drainReport_.closeRequested = closeRequested_;
        drainReport_.closeReady = closeRequested_ && !HasPendingIo();
    }

    bool NrSessionIoActor::IsCloseRequested() const noexcept
    {
        return closeRequested_;
    }
} // namespace psnr::core
