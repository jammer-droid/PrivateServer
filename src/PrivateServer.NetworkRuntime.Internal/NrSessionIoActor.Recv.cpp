#include "pch.h"

#include "NrSessionIoActor.h"

#include "NrErrorCode.h"
#include "NrIngressRegistry.h"
#include "NrInputFactory.h"
#include "NrPacketDispatchTable.h"
#include "NrPacketHeader.h"
#include "NrPacketParser.h"
#include "NrSession.h"
#include "NrSessionIoOperations.h"
#include "NrToWorldHandoff.h"

#include <cassert>

namespace psnr::core
{
    NrStatus NrSessionIoActor::HandleRecvCompleted(const NrSessionIoCompletedEvent& completed) noexcept
    {
        assert(session_ != nullptr);

        const psnr::runtime::NrSessionKey sessionKey = session_->SessionKey();
        if (completed.sessionKey != sessionKey || !session_->HasPendingRecv())
        {
            // session 불일치
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (completed.contextToken != session_->PendingRecv().ContextToken())
        {
            // Token mismatch violates the one-pending-recv invariant.
            // Do not clear the current pending recv; fail fast after requesting close as containment.
            // Recv completion token mismatch violates one-pending-recv invariant.
            assert(completed.contextToken == session_->PendingRecv().ContextToken());
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (completed.status.Succeeded() && completed.bytesTransferred > 0)
        {
            session_->ClearPendingRecv();
            RecordPendingRecvIoCompleted();
            if (IsCloseRequested())
            {
                // Close/drain has already been requested by another lane. This matching
                // completion only releases the pending recv context; do not promote late
                // bytes into application input or post another recv.
                return NrStatus::Success();
            }

            const NrStatus commitStatus = session_->RecvBuffer().CommitWritten(completed.bytesTransferred);
            if (commitStatus.Failed())
            {
                RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
                return commitStatus;
            }

            const NrStatus drainStatus = DrainCommittedRecvFrames();
            if (drainStatus.Failed())
            {
                return drainStatus;
            }

            return RequestNextRecvIfReady();
        }

        // clear/shutdown path
        session_->ClearPendingRecv();
        RecordPendingRecvIoCompleted();
        const psnr::runtime::NrSessionEndReason endReason = completed.status.Succeeded()
                                                                ? psnr::runtime::NrSessionEndReason::RemoteClosed
                                                                : psnr::runtime::NrSessionEndReason::TransportError;
        RecordCloseRequested(endReason);
        return NrStatus::Success();
    }

    NrStatus NrSessionIoActor::DrainCommittedRecvFrames() noexcept
    {
        assert(session_ != nullptr);
        assert(recvDrainDependencies_.IsValid());

        // decode byte stream
        NrRecvBuffer& recvBuffer = session_->RecvBuffer();
        while (recvBuffer.ReadableBytes() > 0)
        {
            NrPacketParseResult parseResult;
            const NrStatus parseStatus =
                recvDrainDependencies_.packetParser->Parse(recvBuffer.ReadableSpan(), parseResult);
            if (parseStatus.Failed())
            {
                RecordCloseRequested(psnr::runtime::NrSessionEndReason::ProtocolError);
                return parseStatus;
            }

            if (parseResult.status == NrPacketParseStatus::NeedMoreData)
            {
                return NrStatus::Success();
            }

            NrPacketDispatchRule dispatchRule;
            const NrStatus dispatchRuleStatus = recvDrainDependencies_.dispatchTable->Find(
                NrPacketType{parseResult.header.packetType}, dispatchRule);
            if (dispatchRuleStatus.Failed())
            {
                RecordCloseRequested(psnr::runtime::NrSessionEndReason::ProtocolError);
                return dispatchRuleStatus;
            }

            if (dispatchRule.dispatchLane == NrDispatchLane::WorldIngress)
            {
                const std::span<const std::byte> payload = parseResult.packetBytes.subspan(NrPacketHeaderLength);
                const NrStatus publicationStatus = recvDrainDependencies_.toWorldHandoff->RecordPacket(
                    session_->SessionKey(), dispatchRule.packetType, payload);
                if (publicationStatus.Failed())
                {
                    const NrErrorCode errorCode = publicationStatus.ErrorCode();
                    const bool receivePressure = errorCode == NrErrorCode::QueueFull ||
                                                 errorCode == NrErrorCode::OutOfMemory ||
                                                 errorCode == NrErrorCode::CapacityExceeded;
                    RecordCloseRequested(receivePressure ? psnr::runtime::NrSessionEndReason::ReceivePressure
                                                         : psnr::runtime::NrSessionEndReason::TransportError);
                    return publicationStatus;
                }

                const NrStatus consumeStatus = recvBuffer.Consume(parseResult.header.packetLength);
                if (consumeStatus.Failed())
                {
                    RecordCloseRequested(psnr::runtime::NrSessionEndReason::ProtocolError);
                    return consumeStatus;
                }

                continue;
            }

            NrResult<NrInput> inputResult =
                recvDrainDependencies_.inputFactory->CreateInput(session_->SessionKey(), parseResult, dispatchRule);
            if (inputResult.Failed())
            {
                if (inputResult.Status().ErrorCode() == NrErrorCode::PoolExhausted)
                {
                    // NrInput payload 생성에 필요한 메모리 풀 고갈
                    // Hard backpressure signal: keep the frame unconsumed and do not post the next recv.
                    // Retry/resume requires a lane low-watermark signal and is intentionally deferred.
                }

                RecordCloseRequested(psnr::runtime::NrSessionEndReason::ReceivePressure);
                return inputResult.Status();
            }

            NrStatus enqueueStatus =
                recvDrainDependencies_.ingressRegistry->TryEnqueue(dispatchRule.dispatchLane, inputResult.TakeValue());
            if (enqueueStatus.Failed())
            {
                if (enqueueStatus.ErrorCode() == NrErrorCode::QueueFull)
                {
                    // Hard backpressure signal: keep the frame unconsumed and do not post the next recv.
                    // Retry/resume requires a lane low-watermark signal and is intentionally deferred.
                }

                RecordCloseRequested(psnr::runtime::NrSessionEndReason::ReceivePressure);
                return enqueueStatus;
            }

            const NrStatus consumeStatus = recvBuffer.Consume(parseResult.header.packetLength);
            if (consumeStatus.Failed())
            {
                RecordCloseRequested(psnr::runtime::NrSessionEndReason::ProtocolError);
                return consumeStatus;
            }
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionIoActor::RequestNextRecvIfReady() noexcept // RecvBuffer에서 남은 데이터 읽기 필요한지 확인
    {
        assert(session_ != nullptr);
        assert(ioOperations_ != nullptr);

        if (drainReport_.closeRequested) // close 요청이 들어온 상태. 신규 PostRecv 불필요
        {
            return NrStatus::Success();
        }

        if (!session_->IsValid() || session_->HasPendingRecv()) // PendingRecv가 없어야 함
        {
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrRecvBuffer& recvBuffer = session_->RecvBuffer();
        if (recvBuffer.WritableBytes() == 0)
        {
            recvBuffer.Compact();
        }

        if (recvBuffer.WritableBytes() == 0) // compact 이후에도 남은 buffer가 없으면 close
        {
            // Future fallback: grow/swap recv buffer storage when dynamic buffer sizing exists.
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::ReceivePressure);
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        RecordPostRecvRequested();
        return NrStatus::Success();
    }

    NrStatus NrSessionIoActor::PostRequestedRecv() noexcept
    {
        assert(session_ != nullptr);
        assert(ioOperations_ != nullptr);

        if (!session_->IsValid() || session_->HasPendingRecv())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        RecordPendingRecvIoStarted();

        const NrStatus postStatus = ioOperations_->PostRecv(*session_);
        if (postStatus.Failed())
        {
            RecordPendingRecvIoCompleted();
            RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
            return postStatus;
        }

        return NrStatus::Success();
    }
} // namespace psnr::core
