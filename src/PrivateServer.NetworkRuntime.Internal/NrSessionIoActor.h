#pragma once

#include "NrActorMailbox.h"
#include "NrSessionEndReason.h"
#include "NrSessionIoEvent.h"
#include "NrSessionSendChannelControl.h"

#include <cstddef>
#include <memory>

namespace psnr::runtime
{
    class NrActorScheduleHandle;
    class NrSession;
    class NrSessionActorLease;
    class NrSessionActorRegistry;
    class NrSessionIoOperations;

    namespace internal
    {
        class NrServerMetrics;
        class NrToWorldHandoff;
    } // namespace internal
} // namespace psnr::runtime

namespace psnr::core
{
    class NrIngressRegistry;
    class NrInputFactory;
    class NrPacketDispatchTable;
    class NrPacketParser;

    struct NrSessionRecvDrainDependencies final
    {
        const NrPacketParser* packetParser = nullptr;
        const NrPacketDispatchTable* dispatchTable = nullptr;
        NrInputFactory* inputFactory = nullptr;
        NrIngressRegistry* ingressRegistry = nullptr;
        psnr::runtime::internal::NrToWorldHandoff* toWorldHandoff = nullptr; // graph-owned, actor보다 오래 유지된다.
        psnr::runtime::internal::NrServerMetrics* serverMetrics = nullptr;   // graph-owned

        [[nodiscard]] bool IsValid() const noexcept
        {
            return packetParser != nullptr && dispatchTable != nullptr && inputFactory != nullptr &&
                   ingressRegistry != nullptr && toWorldHandoff != nullptr && serverMetrics != nullptr;
        }
    };

    struct NrSessionIoDrainReport final
    {
        psnr::runtime::NrSessionEndReason endReason =
            psnr::runtime::NrSessionEndReason::None; // 최초 close 전이가 확정한 종료 사유
        bool closeRequested = false;                 // actor drain 작업 중에 session 을 닫아야 하는 조건이 발생했는지
        bool closeReady = false;                     // close가 요청됐고, 남은 pending IO 가 없는지
        bool requestPostRecv = false;                // actor drain 작업 중에 WSARecv 를 걸어도 된다고 판단했는지
        bool pendingSendQueueFull = false;           // send backlog capacity 초과를 후속 pressure 정책 입력으로 기록
    };

    class NrSessionIoActor final : public INrActor
    {
        using NrAcceptRecvMailbox = NrActorMailbox<NrSessionRecvEvent>;
        using NrSendMailbox = NrActorMailbox<NrSessionSendEvent>;

    public:
        using NrAcceptRecvMailboxHandle = NrActorMailboxHandle<NrSessionRecvEvent>;
        using NrMailboxHandle = NrAcceptRecvMailboxHandle;
        using NrSendMailboxHandle = NrActorMailboxHandle<NrSessionSendEvent>;

        NrSessionIoActor(const NrSessionIoActor&) = delete;
        NrSessionIoActor& operator=(const NrSessionIoActor&) = delete;

        NrSessionIoActor(NrSessionIoActor&&) = delete;
        NrSessionIoActor& operator=(NrSessionIoActor&&) = delete;

        ~NrSessionIoActor() noexcept override;

        [[nodiscard]] static NrResult<std::unique_ptr<NrSessionIoActor>> Create(
            NrMemoryPoolManager& memoryPoolManager, std::size_t mailboxCapacity, std::size_t pendingSendQueueCapacity,
            std::unique_ptr<psnr::runtime::NrSession> session, psnr::runtime::NrSessionIoOperations& ioOperations,
            psnr::runtime::NrActorScheduleHandle actorScheduleHandle,
            psnr::runtime::internal::NrSubmissionAdmissionHandle submissionAdmission,
            NrSessionRecvDrainDependencies recvDrainDependencies) noexcept;

        [[nodiscard]] NrMailboxHandle MailboxHandle(NrActorScheduleGate& scheduleGate) const noexcept;
        [[nodiscard]] NrSendMailboxHandle SendMailboxHandle() const noexcept;
        [[nodiscard]] NrActorScheduleGate* ScheduleGate() const noexcept;

        [[nodiscard]] NrResult<NrActorDrainReport> Drain(NrActorDrainBudget budget) noexcept override;
        [[nodiscard]] NrSessionIoDrainReport TakeDrainReport() noexcept;
        [[nodiscard]] bool HasPendingIo() const noexcept;

    private:
        friend class psnr::runtime::NrSessionActorLease;
        friend class psnr::runtime::NrSessionActorRegistry;

        NrSessionIoActor(std::unique_ptr<NrAcceptRecvMailbox> acceptRecvMailbox,
                         std::unique_ptr<NrSendMailbox> sendMailbox,
                         psnr::runtime::NrSessionSendChannelControlHandle sendChannelControl,
                         std::unique_ptr<NrPayloadRef[]> pendingSendQueue, std::size_t pendingSendQueueCapacity,
                         std::unique_ptr<psnr::runtime::NrSession> session,
                         psnr::runtime::NrSessionIoOperations& ioOperations,
                         NrSessionRecvDrainDependencies recvDrainDependencies) noexcept;

        // Accept lane
        [[nodiscard]] NrStatus HandleAccepted() noexcept;
        void FailCloseAcceptedSession() noexcept;

        // Recv lane
        [[nodiscard]] NrStatus HandleRecvCompleted(const NrSessionIoCompletedEvent& completed) noexcept;
        [[nodiscard]] NrStatus DrainCommittedRecvFrames() noexcept;
        [[nodiscard]] NrStatus RequestNextRecvIfReady() noexcept;
        [[nodiscard]] NrStatus PostRequestedRecv() noexcept;

        // Send lane
        [[nodiscard]] NrStatus HandleSendRequested(NrSessionSendRequestedEvent requested) noexcept;
        [[nodiscard]] NrStatus HandleSendCompleted(const NrSessionIoCompletedEvent& completed) noexcept;
        [[nodiscard]] NrStatus TryPostNextPendingSend() noexcept;
        [[nodiscard]] NrStatus TryPushPendingSend(NrPayloadRef payload) noexcept;
        [[nodiscard]] NrResult<NrPayloadRef> TryPopPendingSend() noexcept;
        [[nodiscard]] bool HasPendingSend() const noexcept;
        void RecordPendingSendQueueFull() noexcept;
        [[nodiscard]] bool IsCloseRequested() const noexcept;

        // Drain loop
        [[nodiscard]] NrResult<bool> TryDrainOneAcceptRecvEvent() noexcept;
        [[nodiscard]] NrResult<bool> TryDrainOneSendEvent() noexcept;

        // Common actor bookkeeping
        void RecordPendingRecvIoStarted() noexcept;
        void RecordPendingRecvIoCompleted() noexcept;
        void RecordPendingSendIoStarted() noexcept;
        void RecordPendingSendIoCompleted() noexcept;
        void RecordCloseRequested(psnr::runtime::NrSessionEndReason endReason) noexcept;
        void RecordServerStopping() noexcept;
        void RecordPostRecvRequested() noexcept;
        void RefreshCloseReady() noexcept;

        std::unique_ptr<NrAcceptRecvMailbox> acceptRecvMailbox_;
        std::unique_ptr<NrSendMailbox> sendMailbox_;
        mutable NrActorScheduleGate scheduleGate_;
        psnr::runtime::NrSessionSendChannelControlHandle sendChannelControl_;
        std::unique_ptr<NrPayloadRef[]> pendingSendQueue_;
        std::size_t pendingSendQueueCapacity_ = 0;
        std::size_t pendingSendQueueHead_ = 0;
        std::size_t pendingSendQueueSize_ = 0;
        std::unique_ptr<psnr::runtime::NrSession> session_;
        psnr::runtime::NrSessionIoOperations* ioOperations_ = nullptr; // non-owning
        NrSessionRecvDrainDependencies recvDrainDependencies_;
        NrSessionIoDrainReport drainReport_;
        std::size_t pendingRecvIoCount_ = 0;
        std::size_t pendingSendIoCount_ = 0;
        // 여러 lane에서 close를 요청할 수 있으므로 최초 원인만 보존하고 후속 요청은 덮어쓰지 않는다.
        psnr::runtime::NrSessionEndReason endReason_ = psnr::runtime::NrSessionEndReason::None;
        bool closeRequested_ = false;
    };
} // namespace psnr::core
