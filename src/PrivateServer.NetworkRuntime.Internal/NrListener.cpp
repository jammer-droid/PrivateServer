#include "pch.h"

#include "NrListener.h"

#include "NrErrorCode.h"
#include "NrIocpPort.h"

#include <cstdint>
#include <new>
#include <utility>
#include <vector>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrListener::NrListener(NrListenerConfig config, NrListenerDependencies dependencies) noexcept
        : config_(config)
        , dependencies_(dependencies)
    {
    }

    NrListener::~NrListener() noexcept
    {
        (void)Shutdown();
    }

    NrStatus NrListener::Configure(NrBootstrapContext&) noexcept
    {
        if (dependencies_.iocpPort == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (config_.acceptSlotCount == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        try
        {
            std::vector<NrAcceptIoContext> acceptContexts;
            acceptContexts.resize(config_.acceptSlotCount);
            acceptContexts_ = std::move(acceptContexts);
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }

    NrStatus NrListener::Start() noexcept
    {
        if (dependencies_.iocpPort == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (acceptContexts_.empty())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus openStatus = listenSocket_.OpenOverlappedTcpIPv4();
        if (openStatus.Failed())
        {
            return openStatus;
        }

        const NrStatus bindStatus = listenSocket_.Bind(config_.bindEndpoint);
        if (bindStatus.Failed())
        {
            (void)listenSocket_.Close();
            return bindStatus;
        }

        const NrStatus listenStatus = listenSocket_.Listen(config_.listenBacklog);
        if (listenStatus.Failed())
        {
            (void)listenSocket_.Close();
            return listenStatus;
        }

        const NrStatus associateStatus =
            dependencies_.iocpPort->AssociateSocket(listenSocket_, reinterpret_cast<std::uintptr_t>(this));
        if (associateStatus.Failed())
        {
            (void)listenSocket_.Close();
            return associateStatus;
        }

        const NrStatus loadAcceptExStatus = listenSocket_.LoadAcceptEx();
        if (loadAcceptExStatus.Failed())
        {
            (void)listenSocket_.Close();
            return loadAcceptExStatus;
        }

        for (NrAcceptIoContext& acceptContext : acceptContexts_)
        {
            const NrStatus postAcceptStatus = PostAccept(acceptContext);
            if (postAcceptStatus.Failed())
            {
                (void)Shutdown();
                return postAcceptStatus;
            }
        }

        return NrStatus::Success();
    }

    NrStatus NrListener::RequestStop(const NrStopContext&) noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrListener::CompleteAccept(NrAcceptIoContext& acceptContext, NrSessionKey sessionKey) noexcept
    {
        if (dependencies_.iocpPort == nullptr || sessionKey == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrWin32Socket& acceptedSocket = acceptContext.AcceptedSocket();
        const NrStatus updateStatus = acceptedSocket.UpdateAcceptContext(listenSocket_);
        if (updateStatus.Failed())
        {
            return updateStatus;
        }

        return dependencies_.iocpPort->AssociateSocket(acceptedSocket, static_cast<std::uintptr_t>(sessionKey));
    }

    NrStatus NrListener::PostAccept(NrAcceptIoContext& acceptContext) noexcept
    {
        acceptContext.ResetForPost();

        NrWin32Socket& acceptedSocket = acceptContext.AcceptedSocket();
        if (acceptedSocket.IsValid())
        {
            const NrStatus status = NrStatus::Failure(NrErrorCode::InvalidState);
            EmitPostFailure(status);
            return status;
        }

        // open accept socket
        const NrStatus openStatus = acceptedSocket.OpenOverlappedTcpIPv4();
        if (openStatus.Failed())
        {
            EmitPostFailure(openStatus);
            return openStatus;
        }

        const NrStatus postStatus = listenSocket_.PostAccept(
            acceptedSocket, acceptContext.Buffer(), acceptContext.InitialReceiveLength(),
            acceptContext.LocalAddressLength(), acceptContext.RemoteAddressLength(), acceptContext.Overlapped());
        if (postStatus.Failed())
        {
            (void)acceptedSocket.Close();
            EmitPostFailure(postStatus);
            return postStatus;
        }

        return NrStatus::Success();
    }

    void NrListener::EmitPostFailure(const NrStatus& status) const noexcept
    {
        internal::NrDiagnosticRecord record;
        record.component = internal::NrDiagnosticComponent::IoPipeline;
        record.operation = internal::NrDiagnosticOperation::Post;
        record.severity = internal::NrDiagnosticSeverity::Error;
        record.eventKind = internal::NrDiagnosticEventKind::Failure;
        record.errorCode = status.ErrorCode();
        record.nativeErrorCode = status.NativeErrorCode();
        record.contextFlags = internal::NrDiagnosticContextFlags::HasIoOperation;
        record.ioOperation = internal::NrDiagnosticIoOperation::Accept;
        dependencies_.diagnosticsEmitter.Emit(record);
    }

    NrStatus NrListener::Shutdown() noexcept
    {
        NrStatus result = listenSocket_.Close();

        for (NrAcceptIoContext& acceptContext : acceptContexts_)
        {
            const NrStatus closeStatus = acceptContext.AcceptedSocket().Close();
            if (result.Succeeded() && closeStatus.Failed())
            {
                result = closeStatus;
            }
        }

        return result;
    }
} // namespace psnr::runtime
