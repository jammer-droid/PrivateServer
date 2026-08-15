#include "pch.h"

#include "NrClientCommandChannel.h"

#include "NrClientControlCompletion.h"
#include "NrErrorCode.h"
#include "NrIocpPort.h"
#include "NrMemoryPoolManager.h"
#include "NrPayloadRef.h"

#include <limits>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrActorAdmissionResolution;
    using psnr::core::NrActorAdmissionTicket;
    using psnr::core::NrActorScheduleDirective;
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    bool NrClientCommandSlot::TryPublish(const NrClientCommand& command) noexcept
    {
        if (!command.IsValid())
        {
            return false;
        }

        State expected = State::Empty;
        // Slot 선점, Empty -> Writing
        if (!state_.compare_exchange_strong(expected, State::Writing, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
        {
            return false;
        }

        // Copy Command
        command_ = command;

        // Writing -> Ready, Slot Release
        state_.store(State::Ready, std::memory_order_release);
        return true;
    }

    bool NrClientCommandSlot::TryConsume(NrClientCommand& outCommand) noexcept
    {
        State expected = State::Ready;
        // Slot 선점, Ready -> Reading
        if (!state_.compare_exchange_strong(expected, State::Reading, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
        {
            return false;
        }

        // command를 outCommand 로 복사하고 Slot의 command 초기화
        outCommand = command_;
        command_ = NrClientCommand{};

        // Reading -> Empty, Slot Release
        state_.store(State::Empty, std::memory_order_release);
        return true;
    }

    NrClientCommandChannel::NrClientCommandChannel(NrIocpPort& iocpPort,
                                                   psnr::core::NrMemoryPoolManager& memoryPoolManager,
                                                   NrClientPendingSendQueue& pendingSendQueue) noexcept
        : iocpPort_(iocpPort)
        , memoryPoolManager_(memoryPoolManager)
        , pendingSendQueue_(pendingSendQueue)
    {
    }

    NrClientCommandChannel::~NrClientCommandChannel() noexcept = default;

    NrStatus NrClientCommandChannel::SubmitConnect(const NrEndpoint& remoteEndpoint,
                                                   std::uint64_t& outAttemptGeneration) noexcept
    {
        if (remoteEndpoint.port == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!iocpPort_.IsValid() || wakeFailure_.load(std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientLifecycleState expectedState = NrClientLifecycleState::Idle;
        // Enter, Idle → TransportConnecting
        if (!state_.compare_exchange_strong(expectedState, NrClientLifecycleState::TransportConnecting,
                                            std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const std::uint64_t observedGeneration = currentGeneration_.load(std::memory_order_relaxed);
        if (observedGeneration == std::numeric_limits<std::uint64_t>::max())
        {
            state_.store(NrClientLifecycleState::Idle, std::memory_order_release);
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        const std::uint64_t attemptGeneration = observedGeneration + 1;
        currentGeneration_.store(attemptGeneration, std::memory_order_release);

        NrActorAdmissionTicket ticket = scheduleGate_.TryBeginAdmission(); // awake idle actor
        if (!ticket.Accepted())
        {
            state_.store(NrClientLifecycleState::Idle, std::memory_order_release);
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        NrClientCommand command;
        command.kind = NrClientCommandKind::Connect;
        command.attemptGeneration = attemptGeneration;
        command.remoteEndpoint = remoteEndpoint;
        if (!pendingConnect_.TryPublish(command))
        {
            static_cast<void>(CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected));
            state_.store(NrClientLifecycleState::Idle, std::memory_order_release);
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        const NrStatus commitStatus =
            CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);
        if (commitStatus.Failed())
        {
            return commitStatus;
        }

        sendAdmissionGeneration_.store(0, std::memory_order_release);
        outAttemptGeneration = attemptGeneration;
        return NrStatus::Success();
    }

    NrStatus NrClientCommandChannel::SubmitSend(const psnr::core::NrPacketType packetType,
                                                const std::span<const std::byte> semanticPayload) noexcept
    {
        if (!iocpPort_.IsValid() || wakeFailure_.load(std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const std::uint64_t attemptGeneration = sendAdmissionGeneration_.load(std::memory_order_acquire);
        if (attemptGeneration == 0 ||
            state_.load(std::memory_order_acquire) != NrClientLifecycleState::TransportConnected)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (!TryReserveSendPipelineSlot()) // reserver send slot first
        {
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        psnr::core::NrResult<psnr::core::NrPayloadRef> payloadResult =
            psnr::core::NrPayloadRefFactory::CreateFramedPayloadRef(memoryPoolManager_, packetType, semanticPayload);
        if (payloadResult.Failed())
        {
            static_cast<void>(TryReleaseSendPipelineSlot());
            return payloadResult.Status();
        }

        NrActorAdmissionTicket ticket = scheduleGate_.TryBeginAdmission();
        if (!ticket.Accepted())
        {
            static_cast<void>(TryReleaseSendPipelineSlot());
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        if (state_.load(std::memory_order_acquire) != NrClientLifecycleState::TransportConnected ||
            sendAdmissionGeneration_.load(std::memory_order_acquire) != attemptGeneration)
        {
            static_cast<void>(CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected));
            static_cast<void>(TryReleaseSendPipelineSlot());
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientPendingSend pendingSend{attemptGeneration, payloadResult.TakeValue()};
        const std::size_t queueDepth = pendingSendQueueDepth_.fetch_add(1, std::memory_order_relaxed) + 1;
        const NrStatus pushStatus = pendingSendQueue_.TryPush(std::move(pendingSend));
        if (pushStatus.Failed())
        {
            static_cast<void>(pendingSendQueueDepth_.fetch_sub(1, std::memory_order_relaxed));
            static_cast<void>(CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected));
            static_cast<void>(TryReleaseSendPipelineSlot());
            return pushStatus;
        }

        UpdatePendingSendQueueHighWatermark(queueDepth);
        return CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);
    }

    NrStatus NrClientCommandChannel::SubmitDisconnect() noexcept
    {
        if (!iocpPort_.IsValid() || wakeFailure_.load(std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientLifecycleState observedState = state_.load(std::memory_order_acquire);
        for (;;)
        {
            if (observedState == NrClientLifecycleState::TransportDisconnecting)
            {
                return NrStatus::Success();
            }

            if (observedState != NrClientLifecycleState::TransportConnecting &&
                observedState != NrClientLifecycleState::TransportConnected)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            if (state_.compare_exchange_weak(observedState, NrClientLifecycleState::TransportDisconnecting,
                                             std::memory_order_acq_rel, std::memory_order_acquire))
            {
                break;
            }
        }

        sendAdmissionGeneration_.store(0, std::memory_order_release);
        NrActorAdmissionTicket ticket = scheduleGate_.TryBeginAdmission();
        if (!ticket.Accepted())
        {
            RecordWakeFailure();
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        NrClientCommand command;
        command.kind = NrClientCommandKind::Disconnect;
        command.attemptGeneration = currentGeneration_.load(std::memory_order_acquire);
        if (!pendingDisconnect_.TryPublish(command))
        {
            static_cast<void>(CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected));
            RecordWakeFailure();
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        return CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);
    }

    NrStatus NrClientCommandChannel::SubmitShutdown() noexcept
    {
        if (state_.load(std::memory_order_acquire) == NrClientLifecycleState::Shutdown)
        {
            return NrStatus::Success();
        }

        if (!iocpPort_.IsValid() || wakeFailure_.load(std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        state_.store(NrClientLifecycleState::Shutdown, std::memory_order_release);
        sendAdmissionGeneration_.store(0, std::memory_order_release);

        NrActorAdmissionTicket ticket = scheduleGate_.TryBeginAdmission();
        if (!ticket.Accepted())
        {
            RecordWakeFailure();
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        NrClientCommand command;
        command.kind = NrClientCommandKind::Shutdown;
        if (!pendingShutdown_.TryPublish(command))
        {
            static_cast<void>(CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected));
            RecordWakeFailure();
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        return CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);
    }

    NrStatus NrClientCommandChannel::SignalEventSpaceAvailable() noexcept
    {
        if (!iocpPort_.IsValid() || wakeFailure_.load(std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (state_.load(std::memory_order_acquire) == NrClientLifecycleState::Shutdown)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrActorAdmissionTicket ticket = scheduleGate_.TryBeginAdmission();
        if (!ticket.Accepted())
        {
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        NrClientCommand command;
        command.kind = NrClientCommandKind::EventSpaceAvailable;
        // 1개가 pop 되어 공간이 남아 있는 상태
        // Drain에서 TryPopCommand 할 때, 사용할 수 있게 publish
        if (!pendingEventSpaceAvailable_.TryPublish(command))
        {
            static_cast<void>(CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected));
            return NrStatus::Success();
        }

        return CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);
    }

    NrStatus NrClientCommandChannel::RecordConnectSucceeded(const std::uint64_t attemptGeneration) noexcept
    {
        if (attemptGeneration == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (attemptGeneration != currentGeneration_.load(std::memory_order_acquire))
        {
            return NrStatus::Success();
        }

        NrClientLifecycleState expectedState = NrClientLifecycleState::TransportConnecting;
        if (state_.compare_exchange_strong(expectedState, NrClientLifecycleState::TransportConnected,
                                           std::memory_order_acq_rel, std::memory_order_acquire))
        {
            sendAdmissionGeneration_.store(attemptGeneration, std::memory_order_release);
            if (state_.load(std::memory_order_acquire) != NrClientLifecycleState::TransportConnected)
            {
                std::uint64_t expectedGeneration = attemptGeneration;
                static_cast<void>(sendAdmissionGeneration_.compare_exchange_strong(
                    expectedGeneration, 0, std::memory_order_acq_rel, std::memory_order_acquire));
            }
            return NrStatus::Success();
        }

        if (expectedState == NrClientLifecycleState::TransportDisconnecting ||
            expectedState == NrClientLifecycleState::Shutdown)
        {
            return NrStatus::Success();
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    NrStatus NrClientCommandChannel::RecordConnectFailed(const std::uint64_t attemptGeneration) noexcept
    {
        if (attemptGeneration == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (attemptGeneration != currentGeneration_.load(std::memory_order_acquire))
        {
            return NrStatus::Success();
        }

        sendAdmissionGeneration_.store(0, std::memory_order_release);
        NrClientLifecycleState expectedState = NrClientLifecycleState::TransportConnecting;
        if (state_.compare_exchange_strong(expectedState, NrClientLifecycleState::Idle, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
        {
            return NrStatus::Success();
        }

        if (expectedState == NrClientLifecycleState::TransportDisconnecting ||
            expectedState == NrClientLifecycleState::Shutdown)
        {
            return NrStatus::Success();
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    NrStatus NrClientCommandChannel::RecordDisconnected(const std::uint64_t attemptGeneration) noexcept
    {
        if (attemptGeneration == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (attemptGeneration != currentGeneration_.load(std::memory_order_acquire))
        {
            return NrStatus::Success();
        }

        sendAdmissionGeneration_.store(0, std::memory_order_release);
        NrClientLifecycleState observedState = state_.load(std::memory_order_acquire);
        while (observedState != NrClientLifecycleState::Shutdown)
        {
            if (observedState != NrClientLifecycleState::TransportDisconnecting &&
                observedState != NrClientLifecycleState::TransportConnected)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            if (state_.compare_exchange_weak(observedState, NrClientLifecycleState::Idle, std::memory_order_acq_rel,
                                             std::memory_order_acquire))
            {
                return NrStatus::Success();
            }
        }

        return NrStatus::Success();
    }

    bool NrClientCommandChannel::TryBeginDrain() noexcept
    {
        return scheduleGate_.TryBeginDrain();
    }

    NrStatus NrClientCommandChannel::CompleteDrain(const bool shouldReschedule) noexcept
    {
        return ApplyScheduleDirective(scheduleGate_.CompleteDrain(shouldReschedule));
    }

    NrStatus NrClientCommandChannel::TryPopCommand(NrClientCommand& outCommand) noexcept
    {
        if (outCommand.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrClientCommandSlot* pendingSlots[] = {
            &pendingConnect_,
            &pendingDisconnect_,
            &pendingShutdown_,
            &pendingEventSpaceAvailable_,
        };

        for (NrClientCommandSlot* pendingSlot : pendingSlots)
        {
            if (pendingSlot->TryConsume(outCommand)) // first return
            {
                return NrStatus::Success();
            }
        }

        return NrStatus::Failure(NrErrorCode::QueueEmpty);
    }

    NrStatus NrClientCommandChannel::TryPopSend(NrClientPendingSend& outSend) noexcept
    {
        if (outSend.attemptGeneration != 0 || !outSend.payload.IsEmpty())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus popStatus = pendingSendQueue_.TryPop(outSend);
        if (popStatus.Succeeded())
        {
            static_cast<void>(pendingSendQueueDepth_.fetch_sub(1, std::memory_order_relaxed));
        }

        return popStatus;
    }

    NrStatus NrClientCommandChannel::CompleteAcceptedSend() noexcept
    {
        return TryReleaseSendPipelineSlot() ? NrStatus::Success() : NrStatus::Failure(NrErrorCode::InvalidState);
    }

    NrStatus NrClientCommandChannel::DiscardPendingSends() noexcept
    {
        while (true)
        {
            NrClientPendingSend pendingSend;
            const NrStatus popStatus = TryPopSend(pendingSend);
            if (popStatus.ErrorCode() == NrErrorCode::QueueEmpty)
            {
                return NrStatus::Success();
            }

            if (popStatus.Failed())
            {
                return popStatus;
            }

            if (!TryReleaseSendPipelineSlot())
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }
        }
    }

    NrClientLifecycleState NrClientCommandChannel::State() const noexcept
    {
        return state_.load(std::memory_order_acquire);
    }

    std::uint64_t NrClientCommandChannel::CurrentGeneration() const noexcept
    {
        return currentGeneration_.load(std::memory_order_acquire);
    }

    psnr::core::NrActorScheduleView NrClientCommandChannel::ScheduleView() const noexcept
    {
        return scheduleGate_.View();
    }

    bool NrClientCommandChannel::HasWakeFailure() const noexcept
    {
        return wakeFailure_.load(std::memory_order_acquire);
    }

    std::size_t NrClientCommandChannel::SendPipelineDepth() const noexcept
    {
        return sendPipelineDepth_.load(std::memory_order_relaxed);
    }

    std::size_t NrClientCommandChannel::PendingSendQueueDepth() const noexcept
    {
        return pendingSendQueueDepth_.load(std::memory_order_relaxed);
    }

    std::size_t NrClientCommandChannel::PendingSendQueueHighWatermark() const noexcept
    {
        return pendingSendQueueHighWatermark_.load(std::memory_order_relaxed);
    }

    NrStatus NrClientCommandChannel::CompleteAdmission(NrActorAdmissionTicket&& ticket,
                                                       const NrActorAdmissionResolution resolution) noexcept
    {
        return ApplyScheduleDirective(scheduleGate_.CompleteAdmission(std::move(ticket), resolution));
    }

    NrStatus NrClientCommandChannel::ApplyScheduleDirective(const NrActorScheduleDirective directive) noexcept
    {
        switch (directive)
        {
        case NrActorScheduleDirective::EnqueueReadyToken:
        {
            const NrStatus wakeStatus =
                PostClientControlCompletion(iocpPort_, NrClientControlCompletionKind::CommandsReady);
            if (wakeStatus.Failed())
            {
                RecordWakeFailure();
            }
            return wakeStatus;
        }

        case NrActorScheduleDirective::NoAction:
        case NrActorScheduleDirective::FinalizationDeferred:
        case NrActorScheduleDirective::ReleasePermit:
            return NrStatus::Success();
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    void NrClientCommandChannel::RecordWakeFailure() noexcept
    {
        wakeFailure_.store(true, std::memory_order_release);
        sendAdmissionGeneration_.store(0, std::memory_order_release);
        state_.store(NrClientLifecycleState::Shutdown, std::memory_order_release);
    }

    bool NrClientCommandChannel::TryReserveSendPipelineSlot() noexcept
    {
        std::size_t observedDepth = sendPipelineDepth_.load(std::memory_order_relaxed);
        while (observedDepth < pendingSendQueue_.Capacity())
        {
            if (sendPipelineDepth_.compare_exchange_weak(observedDepth, observedDepth + 1, std::memory_order_acq_rel,
                                                         std::memory_order_relaxed))
            {
                return true;
            }
        }

        return false;
    }

    bool NrClientCommandChannel::TryReleaseSendPipelineSlot() noexcept
    {
        std::size_t observedDepth = sendPipelineDepth_.load(std::memory_order_relaxed);
        while (observedDepth != 0)
        {
            if (sendPipelineDepth_.compare_exchange_weak(observedDepth, observedDepth - 1, std::memory_order_acq_rel,
                                                         std::memory_order_relaxed))
            {
                return true;
            }
        }

        return false;
    }

    void NrClientCommandChannel::UpdatePendingSendQueueHighWatermark(const std::size_t depth) noexcept
    {
        std::size_t observedHighWatermark = pendingSendQueueHighWatermark_.load(std::memory_order_relaxed);
        while (observedHighWatermark < depth &&
               !pendingSendQueueHighWatermark_.compare_exchange_weak(
                   observedHighWatermark, depth, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }
} // namespace psnr::runtime::internal
