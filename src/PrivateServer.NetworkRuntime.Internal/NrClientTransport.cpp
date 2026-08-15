#include "pch.h"

#include "NrClientTransport.h"

#include "NrClientConnection.h"
#include "NrClientTransportEventSink.h"
#include "NrEndpoint.h"
#include "NrErrorCode.h"
#include "NrIocpPort.h"
#include "NrPacketHeader.h"
#include "NrPacketParser.h"
#include "NrPayloadRef.h"
#include "NrResult.h"
#include "NrSocketAddressWin32.h"

#include <limits>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    NrClientTransport::NrClientTransport(NrIocpPort& iocpPort, psnr::core::NrMemoryPoolManager& memoryPoolManager,
                                         NrClientCommandChannel& commandChannel,
                                         NrClientLifecycleStateMachine& lifecycleStateMachine,
                                         INrClientTransportEventSink& eventSink) noexcept
        : iocpPort_(iocpPort)
        , memoryPoolManager_(memoryPoolManager)
        , commandChannel_(commandChannel)
        , lifecycleStateMachine_(lifecycleStateMachine)
        , eventSink_(eventSink)
    {
    }

    NrClientTransport::~NrClientTransport() noexcept = default;

    NrStatus NrClientTransport::StartConnect(const std::uint64_t attemptGeneration,
                                             const NrEndpoint& remoteEndpoint) noexcept
    {
        if (attemptGeneration == 0 || attemptGeneration != lifecycleStateMachine_.CurrentGeneration())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (lifecycleStateMachine_.State() != NrClientLifecycleState::TransportConnecting || connection_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrSocketAddressWin32 remoteAddress;
        const NrStatus addressStatus = BuildSocketAddressWin32(remoteEndpoint, remoteAddress);
        if (addressStatus.Failed())
        {
            return RecordConnectFailure(addressStatus);
        }

        psnr::core::NrResult<std::unique_ptr<NrClientConnection>> connectionResult =
            NrClientConnection::Create(attemptGeneration, remoteAddress, memoryPoolManager_);
        if (connectionResult.Failed())
        {
            return RecordConnectFailure(connectionResult.Status());
        }

        connection_ = connectionResult.TakeValue();
        const NrStatus startStatus = connection_->StartConnect(iocpPort_);
        if (startStatus.Failed())
        {
            return RecordConnectFailure(startStatus);
        }

        pendingConnectIoCount_.store(1, std::memory_order_release);
        return NrStatus::Success();
    }

    NrStatus NrClientTransport::RequestDisconnect() noexcept
    {
        if (IsDisconnectInProgress()) // Disconnect 요청 중복 처리 방지
        {
            return NrStatus::Success();
        }

        const NrClientLifecycleState lifecycleState = lifecycleStateMachine_.State();
        if (lifecycleState != NrClientLifecycleState::TransportConnecting &&
            lifecycleState != NrClientLifecycleState::TransportConnected &&
            lifecycleState != NrClientLifecycleState::TransportDisconnecting)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return BeginDisconnect(NrClientDisconnectReason::LocalRequested, NrStatus::Success());
    }

    NrStatus NrClientTransport::RequestShutdown() noexcept
    {
        if (shutdownRequested_)
        {
            return NrStatus::Success();
        }

        const NrStatus discardStatus = DiscardQueuedSends();
        if (discardStatus.Failed())
        {
            return discardStatus;
        }

        shutdownRequested_ = true;
        if (connection_ == nullptr)
        {
            return CompleteShutdown();
        }

        pendingDisconnectReason_ = NrClientDisconnectReason::LocalRequested;
        pendingDisconnectStatus_ = NrStatus::Success();
        static_cast<void>(connection_->Close());
        return TryFinishDisconnect();
    }

    NrStatus NrClientTransport::CompleteShutdown() noexcept
    {
        if (!shutdownRequested_ || connection_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus shutdownStatus = lifecycleStateMachine_.Shutdown();
        if (shutdownStatus.Failed() || workerStopPosted_)
        {
            return shutdownStatus;
        }

        const NrStatus stopStatus = PostClientControlCompletion(iocpPort_, NrClientControlCompletionKind::Stop);
        if (stopStatus.Succeeded())
        {
            workerStopPosted_ = true;
        }

        return stopStatus;
    }

    bool NrClientTransport::HasActiveConnection() const noexcept
    {
        return connection_ != nullptr && !IsDisconnectInProgress();
    }

    bool NrClientTransport::HasPendingRecv() const noexcept
    {
        return connection_ != nullptr && connection_->HasPendingRecv();
    }

    bool NrClientTransport::HasPendingSend() const noexcept
    {
        return connection_ != nullptr && connection_->HasPendingSend();
    }

    std::uint64_t NrClientTransport::PendingConnectIoCount() const noexcept
    {
        return pendingConnectIoCount_.load(std::memory_order_acquire);
    }

    std::uint64_t NrClientTransport::PendingRecvIoCount() const noexcept
    {
        return pendingRecvIoCount_.load(std::memory_order_acquire);
    }

    std::uint64_t NrClientTransport::PendingSendIoCount() const noexcept
    {
        return pendingSendIoCount_.load(std::memory_order_acquire);
    }

    std::size_t NrClientTransport::PendingSendQueueDepth() const noexcept
    {
        return commandChannel_.PendingSendQueueDepth();
    }

    std::size_t NrClientTransport::PendingSendQueueHighWatermark() const noexcept
    {
        return commandChannel_.PendingSendQueueHighWatermark();
    }

    NrWin32SocketState NrClientTransport::ConnectionSocketState() const noexcept
    {
        return connection_ == nullptr ? NrWin32SocketState::Closed : connection_->SocketState();
    }

    psnr::core::NrResult<psnr::core::NrActorDrainReport> NrClientTransport::Drain(
        const psnr::core::NrActorDrainBudget budget) noexcept
    {
        if (budget.maxEvents == 0)
        {
            return psnr::core::NrResult<psnr::core::NrActorDrainReport>::Failure(NrErrorCode::InvalidArgument);
        }

        psnr::core::NrActorDrainReport report;
        while (report.drainedCount < budget.maxEvents)
        {
            NrClientCommand command;
            const NrStatus commandStatus = commandChannel_.TryPopCommand(command); // fixed command slot(pending slots)
            if (commandStatus.Succeeded())
            {
                const NrStatus handleStatus = HandleCommand(command);
                if (handleStatus.Failed())
                {
                    return psnr::core::NrResult<psnr::core::NrActorDrainReport>::Failure(handleStatus);
                }

                ++report.drainedCount;
                continue;
            }

            if (commandStatus.ErrorCode() != NrErrorCode::QueueEmpty)
            {
                return psnr::core::NrResult<psnr::core::NrActorDrainReport>::Failure(commandStatus);
            }

            bool consumedSend = false;
            const NrStatus sendStatus = TryConsumeNextSend(consumedSend);
            if (sendStatus.Failed())
            {
                const NrStatus disconnectStatus =
                    connection_ != nullptr && !IsDisconnectInProgress()
                        ? BeginDisconnect(NrClientDisconnectReason::TransportError, sendStatus)
                        : sendStatus;

                if (disconnectStatus.Failed())
                {
                    return psnr::core::NrResult<psnr::core::NrActorDrainReport>::Failure(disconnectStatus);
                }
            }

            if (!consumedSend)
            {
                break;
            }

            ++report.drainedCount;
        }

        report.needsReschedule = (report.drainedCount == budget.maxEvents);
        return psnr::core::NrResult<psnr::core::NrActorDrainReport>(report);
    }

    NrStatus NrClientTransport::HandleCommand(const NrClientCommand& command) noexcept
    {
        switch (command.kind)
        {
        case NrClientCommandKind::Connect:
        {
            const std::uint64_t currentGeneration = lifecycleStateMachine_.CurrentGeneration();
            if (currentGeneration == std::numeric_limits<std::uint64_t>::max() ||
                command.attemptGeneration != currentGeneration + 1)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            std::uint64_t workerGeneration = 0;
            const NrStatus beginStatus = lifecycleStateMachine_.BeginConnect(workerGeneration);
            if (beginStatus.Failed())
            {
                return beginStatus;
            }

            if (workerGeneration != command.attemptGeneration)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            return StartConnect(workerGeneration, command.remoteEndpoint);
        }

        case NrClientCommandKind::Disconnect:
            if (command.attemptGeneration != lifecycleStateMachine_.CurrentGeneration())
            {
                return NrStatus::Success();
            }
            if (connection_ == nullptr && lifecycleStateMachine_.State() == NrClientLifecycleState::Idle)
            {
                return commandChannel_.State() == NrClientLifecycleState::Idle
                           ? NrStatus::Success()
                           : commandChannel_.RecordDisconnected(command.attemptGeneration);
            }
            return RequestDisconnect();

        case NrClientCommandKind::Shutdown:
            return RequestShutdown();

        case NrClientCommandKind::EventSpaceAvailable:
            return eventSink_.HandleEventSpaceAvailable();

        case NrClientCommandKind::None:
        default:
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
    }

    std::uint64_t NrClientTransport::CurrentAttemptGeneration() const noexcept
    {
        return lifecycleStateMachine_.CurrentGeneration();
    }

    NrStatus NrClientTransport::HandleClientControlCompletion(const NrClientControlCompletionKind kind) noexcept
    {
        switch (kind)
        {
        case NrClientControlCompletionKind::EventSpaceAvailable:
            return eventSink_.HandleEventSpaceAvailable();

        case NrClientControlCompletionKind::CommandsReady:
        {
            if (!commandChannel_.TryBeginDrain())
            {
                return NrStatus::Success();
            }

            const psnr::core::NrResult<psnr::core::NrActorDrainReport> drainResult =
                Drain(psnr::core::NrActorDrainBudget{16});
            const bool shouldReschedule = drainResult.Succeeded() && drainResult.Value().needsReschedule;
            const NrStatus completeStatus = commandChannel_.CompleteDrain(shouldReschedule);
            if (drainResult.Failed())
            {
                return drainResult.Status();
            }

            return completeStatus;
        }

        case NrClientControlCompletionKind::Stop:
        case NrClientControlCompletionKind::None:
        default:
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
    }

    NrStatus NrClientTransport::HandleConnectCompletion(NrClientConnectIoContext& context,
                                                        const NrIocpCompletionPacket& packet) noexcept
    {
        if (connection_ == nullptr || &context != &connection_->ConnectContext())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        pendingConnectIoCount_.store(0, std::memory_order_release);

        if (!IsDisconnectInProgress() && commandChannel_.State() == NrClientLifecycleState::TransportDisconnecting)
        {
            const NrStatus disconnectStatus = RequestDisconnect();
            if (disconnectStatus.Failed())
            {
                return disconnectStatus;
            }
        }

        if (!shutdownRequested_ && commandChannel_.State() == NrClientLifecycleState::Shutdown)
        {
            const NrStatus shutdownStatus = RequestShutdown();
            if (shutdownStatus.Failed())
            {
                return shutdownStatus;
            }
        }

        if (IsDisconnectInProgress())
        {
            return TryFinishDisconnect();
        }

        if (packet.ioStatus.Failed())
        {
            return RecordConnectFailure(packet.ioStatus);
        }

        const NrStatus updateStatus = connection_->CompleteConnect();
        if (updateStatus.Failed())
        {
            return RecordConnectFailure(updateStatus);
        }

        const NrStatus recvStatus = connection_->PostRecv();
        if (recvStatus.Failed())
        {
            return RecordConnectFailure(recvStatus);
        }

        pendingRecvIoCount_.store(1, std::memory_order_release);
        return RecordConnectSuccess();
    }

    NrStatus NrClientTransport::HandleRecvCompletion(NrRecvIoContext& context,
                                                     const NrIocpCompletionPacket& packet) noexcept
    {
        if (connection_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (&context != &connection_->RecvContext())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        const bool wasPending = connection_->HasPendingRecv();
        const NrStatus completionStatus = connection_->CompleteRecv(context, packet);
        if (!wasPending)
        {
            return completionStatus;
        }

        pendingRecvIoCount_.store(0, std::memory_order_release);

        if (IsDisconnectInProgress())
        {
            return TryFinishDisconnect();
        }

        if (completionStatus.Failed())
        {
            return BeginDisconnect(NrClientDisconnectReason::TransportError, completionStatus);
        }

        if (packet.bytesTransferred == 0)
        {
            return BeginDisconnect(NrClientDisconnectReason::RemoteClosed, NrStatus::Success());
        }

        while (connection_->ReadableRecvBytes() > 0)
        {
            psnr::core::NrPacketParseResult parseResult;
            const NrStatus parseStatus = connection_->ParseNextReceivedPacket(parseResult);
            if (parseStatus.Failed())
            {
                return BeginDisconnect(NrClientDisconnectReason::ProtocolError, parseStatus);
            }

            if (parseResult.status == psnr::core::NrPacketParseStatus::NeedMoreData)
            {
                break;
            }

            if (!lifecycleStateMachine_.CanPublishPacket())
            {
                return BeginDisconnect(NrClientDisconnectReason::TransportError,
                                       NrStatus::Failure(NrErrorCode::InvalidState));
            }

            const std::span<const std::byte> payload =
                parseResult.packetBytes.subspan(psnr::core::NrPacketHeaderLength);
            const NrStatus publishStatus =
                eventSink_.PublishPacketReceived(psnr::core::NrPacketType{parseResult.header.packetType}, payload);
            if (publishStatus.Failed())
            {
                const NrClientDisconnectReason reason = publishStatus.ErrorCode() == NrErrorCode::QueueFull
                                                            ? NrClientDisconnectReason::ReceivePressure
                                                            : NrClientDisconnectReason::TransportError;
                return BeginDisconnect(reason, publishStatus);
            }

            const NrStatus consumeStatus = connection_->ConsumeReceivedPacket(parseResult.header.packetLength);
            if (consumeStatus.Failed())
            {
                return BeginDisconnect(NrClientDisconnectReason::ProtocolError, consumeStatus);
            }
        }

        const NrStatus postStatus = connection_->PostRecv();
        if (postStatus.Failed())
        {
            return BeginDisconnect(NrClientDisconnectReason::TransportError, postStatus);
        }

        pendingRecvIoCount_.store(1, std::memory_order_release);
        return NrStatus::Success();
    }

    NrStatus NrClientTransport::HandleSendCompletion(NrSendIoContext& context,
                                                     const NrIocpCompletionPacket& packet) noexcept
    {
        if (connection_ == nullptr || &context != &connection_->SendContext())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        const bool wasPending = connection_->HasPendingSend();
        NrClientSendCompletionResult result = NrClientSendCompletionResult::None;
        const NrStatus completionStatus = connection_->CompleteSend(context, packet, result);
        if (!wasPending)
        {
            return completionStatus;
        }

        pendingSendIoCount_.store(0, std::memory_order_release);

        if (IsDisconnectInProgress()) // just drain after disconnect requested
        {
            ReleaseActiveSendAdmission();
            return TryFinishDisconnect();
        }

        if (lifecycleStateMachine_.State() == NrClientLifecycleState::TransportDisconnecting)
        {
            ReleaseActiveSendAdmission();
            return NrStatus::Success();
        }

        if (completionStatus.Failed())
        {
            ReleaseActiveSendAdmission();
            return BeginDisconnect(NrClientDisconnectReason::TransportError, completionStatus);
        }

        if (result == NrClientSendCompletionResult::Completed)
        {
            ReleaseActiveSendAdmission();
            const NrStatus startStatus = TryStartNextSend();
            return startStatus.Failed() ? BeginDisconnect(NrClientDisconnectReason::TransportError, startStatus)
                                        : startStatus;
        }

        if (result != NrClientSendCompletionResult::NeedsRepost)
        {
            ReleaseActiveSendAdmission();
            return BeginDisconnect(NrClientDisconnectReason::TransportError,
                                   NrStatus::Failure(NrErrorCode::InvalidState));
        }

        const NrStatus repostStatus = connection_->RepostSend();
        if (repostStatus.Failed())
        {
            ReleaseActiveSendAdmission();
            return BeginDisconnect(NrClientDisconnectReason::TransportError, repostStatus);
        }

        pendingSendIoCount_.store(1, std::memory_order_release);
        return NrStatus::Success();
    }

    NrStatus NrClientTransport::HandleStaleIoCompletion(const NrIoOperationType, const std::uint64_t,
                                                        const NrIocpCompletionPacket&) noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrClientTransport::RecordConnectSuccess() noexcept
    {
        if (connection_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus recordStatus = lifecycleStateMachine_.RecordConnectSucceeded(connection_->AttemptGeneration());
        if (recordStatus.Failed())
        {
            return recordStatus;
        }

        const NrStatus publishStatus = eventSink_.PublishTransportConnected();
        if (publishStatus.Failed())
        {
            return publishStatus;
        }

        const NrStatus commitStatus = lifecycleStateMachine_.CommitNextPending();
        if (commitStatus.Failed())
        {
            return commitStatus;
        }

        return commandChannel_.RecordConnectSucceeded(connection_->AttemptGeneration());
    }

    NrStatus NrClientTransport::RecordConnectFailure(const NrStatus transportStatus) noexcept
    {
        if (transportStatus.Succeeded())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (connection_ != nullptr)
        {
            (void)connection_->Close();
            connection_.reset();
        }

        const std::uint64_t attemptGeneration = lifecycleStateMachine_.CurrentGeneration();
        const NrStatus recordStatus = lifecycleStateMachine_.RecordConnectFailed(attemptGeneration, transportStatus);
        if (recordStatus.Failed())
        {
            return recordStatus;
        }

        const NrStatus publishStatus = eventSink_.PublishTransportConnectionFailed(transportStatus);
        if (publishStatus.Failed())
        {
            return publishStatus;
        }

        const NrStatus commitStatus = lifecycleStateMachine_.CommitNextPending();
        return commitStatus.Failed() ? commitStatus : commandChannel_.RecordConnectFailed(attemptGeneration);
    }

    NrStatus NrClientTransport::TryStartNextSend() noexcept
    {
        if (connection_ == nullptr || IsDisconnectInProgress())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (commandChannel_.State() != NrClientLifecycleState::TransportConnected)
        {
            return NrStatus::Success();
        }

        if (lifecycleStateMachine_.State() == NrClientLifecycleState::TransportDisconnecting)
        {
            return NrStatus::Success();
        }

        if (!lifecycleStateMachine_.CanSend())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (connection_->HasPendingSend()) // pending 이 있으면 즉시 return
        {
            return NrStatus::Success();
        }

        for (;;)
        {
            bool consumedSend = false;
            const NrStatus consumeStatus = TryConsumeNextSend(consumedSend);
            if (consumeStatus.Failed() || !consumedSend || connection_->HasPendingSend())
            {
                return consumeStatus;
            }
        }
    }

    NrStatus NrClientTransport::TryConsumeNextSend(bool& outConsumed) noexcept
    {
        outConsumed = false;
        if (connection_ != nullptr && connection_->HasPendingSend())
        {
            return NrStatus::Success();
        }

        NrClientPendingSend pendingSend;
        const NrStatus popStatus = commandChannel_.TryPopSend(pendingSend);
        if (popStatus.ErrorCode() == NrErrorCode::QueueEmpty)
        {
            return NrStatus::Success();
        }

        if (popStatus.Failed())
        {
            return popStatus;
        }

        outConsumed = true;
        if (connection_ == nullptr || IsDisconnectInProgress() ||
            commandChannel_.State() != NrClientLifecycleState::TransportConnected ||
            lifecycleStateMachine_.State() == NrClientLifecycleState::TransportDisconnecting ||
            pendingSend.attemptGeneration != connection_->AttemptGeneration())
        {
            return commandChannel_.CompleteAcceptedSend();
        }

        if (!lifecycleStateMachine_.CanSend())
        {
            static_cast<void>(commandChannel_.CompleteAcceptedSend());
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus postStatus = connection_->PostSend(std::move(pendingSend.payload));
        if (postStatus.Failed())
        {
            static_cast<void>(commandChannel_.CompleteAcceptedSend());
            return postStatus;
        }

        pendingSendIoCount_.store(1, std::memory_order_release);
        activeSendUsesAdmission_ = true;
        return NrStatus::Success();
    }

    NrStatus NrClientTransport::DiscardQueuedSends() noexcept
    {
        return commandChannel_.DiscardPendingSends();
    }

    void NrClientTransport::ReleaseActiveSendAdmission() noexcept
    {
        if (activeSendUsesAdmission_)
        {
            activeSendUsesAdmission_ = false;
            static_cast<void>(commandChannel_.CompleteAcceptedSend());
        }
    }

    bool NrClientTransport::IsDisconnectInProgress() const noexcept
    {
        return pendingDisconnectReason_ != NrClientDisconnectReason::None;
    }

    NrStatus NrClientTransport::BeginDisconnect(const NrClientDisconnectReason reason,
                                                const NrStatus transportStatus) noexcept
    {
        if (reason == NrClientDisconnectReason::None || connection_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!IsDisconnectInProgress())
        {
            const NrStatus discardStatus = DiscardQueuedSends();
            if (discardStatus.Failed())
            {
                return discardStatus;
            }

            const bool wasConnecting = (lifecycleStateMachine_.State() == NrClientLifecycleState::TransportConnecting);
            const NrStatus requestStatus = lifecycleStateMachine_.RequestDisconnect();
            if (requestStatus.Failed())
            {
                return requestStatus;
            }

            pendingDisconnectReason_ = reason;
            pendingDisconnectStatus_ = transportStatus;
            (void)connection_->Close();
            if (wasConnecting)
            {
                return NrStatus::Success();
            }
        }

        return TryFinishDisconnect();
    }

    NrStatus NrClientTransport::TryFinishDisconnect() noexcept
    {
        if (!IsDisconnectInProgress() || connection_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const bool hasPendingConnect = pendingConnectIoCount_.load(std::memory_order_acquire) != 0;
        if (hasPendingConnect || connection_->HasPendingRecv() || connection_->HasPendingSend())
        {
            return NrStatus::Success();
        }

        const std::uint64_t attemptGeneration = connection_->AttemptGeneration();
        connection_.reset(); // reset unique_ptr first

        if (shutdownRequested_)
        {
            pendingDisconnectReason_ = NrClientDisconnectReason::None;
            pendingDisconnectStatus_ = NrStatus::Success();
            return CompleteShutdown();
        }

        const NrStatus recordStatus = lifecycleStateMachine_.RecordDisconnected(
            attemptGeneration, pendingDisconnectReason_, pendingDisconnectStatus_);
        if (recordStatus.Failed())
        {
            return recordStatus;
        }

        const NrStatus publishStatus =
            eventSink_.PublishTransportDisconnected(pendingDisconnectReason_, pendingDisconnectStatus_);
        if (publishStatus.Failed())
        {
            return publishStatus;
        }

        const NrStatus commitStatus = lifecycleStateMachine_.CommitNextPending();
        if (commitStatus.Failed())
        {
            return commitStatus;
        }

        const NrStatus channelStatus = commandChannel_.RecordDisconnected(attemptGeneration);
        if (channelStatus.Failed())
        {
            return channelStatus;
        }

        pendingDisconnectReason_ = NrClientDisconnectReason::None;
        pendingDisconnectStatus_ = NrStatus::Success();
        return NrStatus::Success();
    }
} // namespace psnr::runtime::internal
