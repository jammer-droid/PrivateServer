#include "pch.h"

#include "NrIoEventDispatcher.h"

#include "NrErrorCode.h"
#include "NrListener.h"
#include "NrMemoryPoolManager.h"
#include "NrRecvIoContext.h"
#include "NrSession.h"
#include "NrSessionActorRegistry.h"
#include "NrSessionIoActor.h"
#include "NrSessionIoOperations.h"
#include "NrToWorldHandoff.h"

#include <cassert>
#include <limits>
#include <memory>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrResult;
    using psnr::core::NrSessionIoActor;
    using psnr::core::NrSessionIoCompletedEvent;
    using psnr::core::NrSessionRecvEvent;
    using psnr::core::NrSessionRecvEventType;
    using psnr::core::NrSessionSendEvent;
    using psnr::core::NrSessionSendEventType;

    namespace
    {
        [[nodiscard]] NrStatus ValidateType(const NrIoEvent& event, NrIoOperationType expectedType) noexcept
        {
            if (event.OperationType() != expectedType)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            return NrStatus::Success();
        }

        [[nodiscard]] constexpr internal::NrDiagnosticIoOperation MapDiagnosticIoOperation(
            const NrIoOperationType operationType) noexcept
        {
            switch (operationType)
            {
            case NrIoOperationType::Accept:
                return internal::NrDiagnosticIoOperation::Accept;
            case NrIoOperationType::Recv:
                return internal::NrDiagnosticIoOperation::Receive;
            case NrIoOperationType::Send:
                return internal::NrDiagnosticIoOperation::Send;
            case NrIoOperationType::Connect: // for Client Connect
            case NrIoOperationType::Unknown:
                return internal::NrDiagnosticIoOperation::Unknown;
            }

            return internal::NrDiagnosticIoOperation::Unknown;
        }

        void EmitCompletionFailure(const NrIoEvent& event, const NrIoOperationType operationType,
                                   const internal::NrDiagnosticEmitter diagnosticsEmitter) noexcept
        {
            const NrStatus status = event.Status();
            if (status.Succeeded())
            {
                return;
            }

            internal::NrDiagnosticRecord record;
            record.component = internal::NrDiagnosticComponent::IoPipeline;
            record.operation = internal::NrDiagnosticOperation::Complete;
            record.severity = internal::NrDiagnosticSeverity::Error;
            record.eventKind = internal::NrDiagnosticEventKind::Failure;
            record.errorCode = status.ErrorCode();
            record.nativeErrorCode = status.NativeErrorCode();
            record.contextFlags = internal::NrDiagnosticContextFlags::HasIoOperation;
            record.ioOperation = MapDiagnosticIoOperation(operationType);

            if (event.SessionKey() != 0)
            {
                record.sessionKey = event.SessionKey();
                record.contextFlags = static_cast<internal::NrDiagnosticContextFlags>(
                    static_cast<std::uint8_t>(record.contextFlags) |
                    static_cast<std::uint8_t>(internal::NrDiagnosticContextFlags::HasSessionKey));
            }

            diagnosticsEmitter.Emit(record);
        }

        [[nodiscard]] NrResult<NrSessionIoCompletedEvent> CreateSessionIoCompletedEvent(
            const NrIoEvent& event, NrIoOperationType expectedType, const void* contextTokenSource,
            const NrIoEventDispatcherDependencies& dependencies) noexcept
        {
            const NrStatus typeStatus = ValidateType(event, expectedType);
            if (typeStatus.Failed())
            {
                return NrResult<NrSessionIoCompletedEvent>::Failure(typeStatus);
            }

            EmitCompletionFailure(event, expectedType, dependencies.diagnosticsEmitter);

            if (contextTokenSource == nullptr || !dependencies.actorScheduleHandle.IsValid())
            {
                return NrResult<NrSessionIoCompletedEvent>::Failure(NrErrorCode::InvalidState);
            }

            NrSessionIoCompletedEvent completed;
            completed.sessionKey = event.SessionKey();
            completed.bytesTransferred = event.BytesTransferred();
            completed.status = event.Status();
            completed.contextToken = contextTokenSource;

            return NrResult<NrSessionIoCompletedEvent>(completed);
        }

        [[nodiscard]] NrStatus RouteSessionActorRecvCompletion(
            const NrIoEvent& event, NrIoOperationType expectedType, const void* contextTokenSource,
            const NrIoEventDispatcherDependencies& dependencies) noexcept
        {
            NrResult<NrSessionIoCompletedEvent> completedResult =
                CreateSessionIoCompletedEvent(event, expectedType, contextTokenSource, dependencies);
            if (completedResult.Failed())
            {
                return completedResult.Status();
            }

            return dependencies.actorScheduleHandle.Enqueue(
                event.SessionKey(), NrSessionRecvEvent{NrSessionRecvEventType::RecvCompleted, completedResult.Value()});
        }

        [[nodiscard]] NrStatus RouteSessionActorSendCompletion(
            const NrIoEvent& event, NrIoOperationType expectedType, const void* contextTokenSource,
            const NrIoEventDispatcherDependencies& dependencies) noexcept
        {
            NrResult<NrSessionIoCompletedEvent> completedResult =
                CreateSessionIoCompletedEvent(event, expectedType, contextTokenSource, dependencies);
            if (completedResult.Failed())
            {
                return completedResult.Status();
            }

            return dependencies.actorScheduleHandle.Enqueue(
                event.SessionKey(),
                NrSessionSendEvent{NrSessionSendEventType::SendCompleted, {}, completedResult.Value()});
        }

        [[nodiscard]] NrStatus BootstrapAcceptedSessionActor(
            NrSessionKey sessionKey, NrSession&& session, const NrIoEventDispatcherDependencies& dependencies) noexcept
        {
            // Runtime registry에 actor를 노출하기 전에 World publication slot을 먼저 확보한다.
            const NrStatus reservationStatus = dependencies.toWorldHandoff->ReserveSession(sessionKey);
            if (reservationStatus.Failed())
            {
                return reservationStatus;
            }

            std::unique_ptr<NrSession> actorSession(new (std::nothrow) NrSession(std::move(session)));
            if (actorSession == nullptr)
            {
                static_cast<void>(dependencies.toWorldHandoff->CancelSessionReservation(sessionKey));
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            NrResult<std::unique_ptr<NrSessionIoActor>> actorResult = NrSessionIoActor::Create(
                *dependencies.memoryPoolManager, dependencies.actorMailboxCapacity,
                dependencies.pendingSendQueueCapacity, std::move(actorSession), *dependencies.sessionIoOperations,
                dependencies.actorScheduleHandle, dependencies.submissionAdmission,
                *dependencies.recvDrainDependencies);
            if (actorResult.Failed())
            {
                static_cast<void>(dependencies.toWorldHandoff->CancelSessionReservation(sessionKey));
                return actorResult.Status();
            }

            const NrStatus actorRegisterStatus =
                dependencies.sessionActorRegistry->TryRegisterActor(sessionKey, actorResult.TakeValue());
            if (actorRegisterStatus.Failed())
            {
                static_cast<void>(dependencies.toWorldHandoff->CancelSessionReservation(sessionKey));
                return actorRegisterStatus;
            }

            const NrStatus scheduleStatus = dependencies.actorScheduleHandle.Enqueue(
                sessionKey, NrSessionRecvEvent{NrSessionRecvEventType::Accepted, {}});
            if (scheduleStatus.Failed())
            {
                static_cast<void>(dependencies.sessionActorRegistry->RequestClose(sessionKey));
                static_cast<void>(dependencies.sessionActorRegistry->DeregisterClosedActors());
                static_cast<void>(dependencies.toWorldHandoff->CancelSessionReservation(sessionKey));
                return scheduleStatus;
            }

            return NrStatus::Success();
        }
    } // namespace

    NrIoEventDispatcher::NrIoEventDispatcher(NrIoEventDispatcherDependencies dependencies) noexcept
        : dependencies_(dependencies)
    {
    }

    NrStatus NrIoEventDispatcher::DispatchAccept(const NrIoEvent& event, NrAcceptIoContext& context) noexcept
    {
        const NrStatus typeStatus = ValidateType(event, NrIoOperationType::Accept);
        if (typeStatus.Failed())
        {
            return typeStatus;
        }

        EmitCompletionFailure(event, NrIoOperationType::Accept, dependencies_.diagnosticsEmitter);

        if (event.Status().Failed())
        {
            return event.Status();
        }

        const NrStatus dependencyStatus = ValidateAcceptDependencies();
        if (dependencyStatus.Failed())
        {
            return dependencyStatus;
        }

        NrSessionKey sessionKey = 0;
        const NrStatus sessionKeyStatus = AllocateSessionKey(sessionKey);
        if (sessionKeyStatus.Failed())
        {
            return sessionKeyStatus;
        }

        const NrStatus completeStatus = dependencies_.listener->CompleteAccept(context, sessionKey);
        if (completeStatus.Failed())
        {
            return completeStatus;
        }

        NrResult<NrSession> sessionResult =
            NrSession::Create(sessionKey, context.TakeAcceptedSocket(), *dependencies_.memoryPoolManager,
                              dependencies_.recvBufferCapacity);
        if (sessionResult.Failed())
        {
            return RecordSessionBootstrapFailure(sessionKey, sessionResult.Status());
        }

        const NrStatus bootstrapStatus =
            BootstrapAcceptedSessionActor(sessionKey, sessionResult.TakeValue(), dependencies_);
        if (bootstrapStatus.Failed())
        {
            return RecordSessionBootstrapFailure(sessionKey, bootstrapStatus);
        }

        return dependencies_.listener->PostAccept(context); // post new accept-ex
    }

    NrStatus NrIoEventDispatcher::DispatchRecv(const NrIoEvent& event, NrRecvIoContext& context) noexcept
    {
        return RouteSessionActorRecvCompletion(event, NrIoOperationType::Recv, &context, dependencies_);
    }

    NrStatus NrIoEventDispatcher::DispatchSend(const NrIoEvent& event, NrSendIoContext& context) noexcept
    {
        return RouteSessionActorSendCompletion(event, NrIoOperationType::Send, &context, dependencies_);
    }

    NrStatus NrIoEventDispatcher::ValidateAcceptDependencies() const noexcept
    {
        if (dependencies_.listener == nullptr || dependencies_.sessionActorRegistry == nullptr ||
            dependencies_.sessionIoOperations == nullptr || dependencies_.toWorldHandoff == nullptr ||
            !dependencies_.actorScheduleHandle.IsValid() ||
            !dependencies_.submissionAdmission.IsValid() ||
            dependencies_.memoryPoolManager == nullptr || dependencies_.recvBufferCapacity == 0 ||
            dependencies_.actorMailboxCapacity == 0 || dependencies_.recvDrainDependencies == nullptr ||
            dependencies_.pendingSendQueueCapacity == 0 || !dependencies_.recvDrainDependencies->IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return NrStatus::Success();
    }

    NrStatus NrIoEventDispatcher::AllocateSessionKey(NrSessionKey& outSessionKey) noexcept
    {
        if (nextSessionKey_ == 0 || nextSessionKey_ == std::numeric_limits<NrSessionKey>::max())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        outSessionKey = nextSessionKey_;
        ++nextSessionKey_;
        return NrStatus::Success();
    }

    NrStatus NrIoEventDispatcher::RecordSessionBootstrapFailure(const NrSessionKey sessionKey,
                                                                const NrStatus& status) const noexcept
    {
        assert(sessionKey != 0);
        assert(status.Failed());

        internal::NrDiagnosticRecord record;
        record.sessionKey = sessionKey;
        record.severity = internal::NrDiagnosticSeverity::Error;
        record.eventKind = internal::NrDiagnosticEventKind::Failure;
        record.errorCode = status.ErrorCode();
        record.nativeErrorCode = status.NativeErrorCode();
        record.contextFlags = internal::NrDiagnosticContextFlags::HasSessionKey;

        if (status.ErrorCode() == NrErrorCode::PoolExhausted)
        {
            record.component = internal::NrDiagnosticComponent::MemoryPool;
            record.operation = internal::NrDiagnosticOperation::Acquire;
        }
        else
        {
            record.component = internal::NrDiagnosticComponent::Session;
            record.operation = internal::NrDiagnosticOperation::Admission;
        }

        dependencies_.diagnosticsEmitter.Emit(record);
        return status;
    }
} // namespace psnr::runtime
