#include "pch.h"

#include "NrSessionIoOperations.h"

#include "NrErrorCode.h"
#include "NrIocpOverlappedContextFactory.h"
#include "NrIoOperationType.h"
#include "NrRecvBuffer.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrServerMetrics.h"
#include "NrSession.h"
#include "NrWin32Socket.h"

#include <cstddef>
#include <span>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrResult;

    NrSessionIoOperations::NrSessionIoOperations(NrIocpOverlappedContextFactory& contextFactory,
                                                 internal::NrServerMetrics& metrics,
                                                 internal::NrDiagnosticEmitter diagnosticsEmitter) noexcept
        : contextFactory_(contextFactory)
        , metrics_(metrics)
        , diagnosticsEmitter_(diagnosticsEmitter)
    {
    }

    NrStatus NrSessionIoOperations::PostRecv(NrSession& session) noexcept
    {
        if (!session.IsValid() || session.Socket().State() != NrWin32SocketState::ConnectedTcpIPv4)
        {
            return RecordRecvPostFailure(session, NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        if (session.HasPendingRecv()) // one out standing recv policy
        {
            return RecordRecvPostFailure(session, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        NrRecvBuffer& recvBuffer = session.RecvBuffer();
        if (recvBuffer.WritableBytes() == 0)
        {
            recvBuffer.Compact();
        }

        std::span<std::byte> writableBuffer = recvBuffer.WritableSpan();
        if (writableBuffer.empty())
        {
            return RecordRecvPostFailure(session, NrStatus::Failure(NrErrorCode::CapacityExceeded));
        }

        NrResult<NrRecvIoContextLease> pendingRecvResult =
            contextFactory_.CreateRecv(session.SessionKey(), writableBuffer);
        if (pendingRecvResult.Failed())
        {
            return RecordRecvPostFailure(session, pendingRecvResult.Status());
        }

        NrStatus pendingStatus = session.SetPendingRecv(pendingRecvResult.TakeValue());
        if (pendingStatus.Failed())
        {
            return RecordRecvPostFailure(session, pendingStatus);
        }

        NrRecvIoContext& context = session.PendingRecv().Context();
        const NrStatus postStatus = session.Socket().PostRecv(context.wsabuf, session.PendingRecv().Overlapped());
        if (postStatus.Failed())
        {
            session.ClearPendingRecv();
            return RecordRecvPostFailure(session, postStatus);
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionIoOperations::PostSend(NrSession& session, NrPayloadRef payload) noexcept
    {
        if (!session.IsValid() || session.Socket().State() != NrWin32SocketState::ConnectedTcpIPv4 || payload.IsEmpty())
        {
            return RecordSendPostFailure(session, NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        if (session.HasPendingSend())
        {
            return RecordSendPostFailure(session, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        NrResult<NrSendIoContextLease> pendingSendResult =
            contextFactory_.CreateSend(session.SessionKey(), std::move(payload));
        if (pendingSendResult.Failed())
        {
            metrics_.Record(NrPressureTransactionOutcome::SendResourceAcquireFailed);
            return RecordSendPostFailure(session, pendingSendResult.Status());
        }

        NrStatus pendingStatus = session.SetPendingSend(pendingSendResult.TakeValue());
        if (pendingStatus.Failed())
        {
            return RecordSendPostFailure(session, pendingStatus);
        }

        NrSendIoContext& context = session.PendingSend().Context();
        const NrStatus postStatus = session.Socket().PostSend(context.wsabuf, session.PendingSend().Overlapped());
        if (postStatus.Failed())
        {
            session.ClearPendingSend();
            metrics_.Record(NrPressureTransactionOutcome::NativeSendPostFailed);
            return RecordSendPostFailure(session, postStatus);
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionIoOperations::RepostPendingSend(NrSession& session) noexcept
    {
        if (!session.IsValid() || session.Socket().State() != NrWin32SocketState::ConnectedTcpIPv4 ||
            !session.HasPendingSend())
        {
            return RecordSendPostFailure(session, NrStatus::Failure(NrErrorCode::InvalidArgument));
        }

        NrSendIoContext& context = session.PendingSend().Context();
        if (context.IsFullySent() || context.wsabuf.buf == nullptr || context.wsabuf.len == 0)
        {
            return RecordSendPostFailure(session, NrStatus::Failure(NrErrorCode::InvalidState));
        }

        context.header.Reset(NrIoOperationType::Send);

        const NrStatus postStatus = session.Socket().PostSend(context.wsabuf, session.PendingSend().Overlapped());
        if (postStatus.Failed())
        {
            session.ClearPendingSend();
            metrics_.Record(NrPressureTransactionOutcome::NativeSendPostFailed);
            return RecordSendPostFailure(session, postStatus);
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionIoOperations::RecordRecvPostFailure(const NrSession& session,
                                                          const NrStatus status) const noexcept
    {
        EmitPostFailure(internal::NrDiagnosticIoOperation::Receive, session, status);
        return status;
    }

    NrStatus NrSessionIoOperations::RecordSendPostFailure(const NrSession& session,
                                                          const NrStatus status) const noexcept
    {
        EmitPostFailure(internal::NrDiagnosticIoOperation::Send, session, status);
        return status;
    }

    void NrSessionIoOperations::EmitPostFailure(const internal::NrDiagnosticIoOperation ioOperation,
                                                const NrSession& session, const NrStatus& status) const noexcept
    {
        internal::NrDiagnosticRecord record;
        record.component = internal::NrDiagnosticComponent::IoPipeline;
        record.operation = internal::NrDiagnosticOperation::Post;
        record.severity = internal::NrDiagnosticSeverity::Error;
        record.eventKind = internal::NrDiagnosticEventKind::Failure;
        record.errorCode = status.ErrorCode();
        record.nativeErrorCode = status.NativeErrorCode();
        record.contextFlags = internal::NrDiagnosticContextFlags::HasIoOperation;
        record.ioOperation = ioOperation;

        const psnr::core::NrSessionKey sessionKey = session.SessionKey();
        if (sessionKey != 0)
        {
            record.sessionKey = sessionKey;
            record.contextFlags = static_cast<internal::NrDiagnosticContextFlags>(
                static_cast<std::uint8_t>(record.contextFlags) |
                static_cast<std::uint8_t>(internal::NrDiagnosticContextFlags::HasSessionKey));
        }

        diagnosticsEmitter_.Emit(record);
    }
} // namespace psnr::runtime
