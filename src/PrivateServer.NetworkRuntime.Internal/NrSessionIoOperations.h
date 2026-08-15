#pragma once

#include "NrDiagnosticEmitter.h"
#include "NrPayloadRef.h"
#include "NrStatus.h"

namespace psnr::runtime
{
    class NrIocpOverlappedContextFactory;
    class NrSession;

    namespace internal
    {
        class NrServerMetrics;
    }

    using psnr::core::NrStatus;
    using psnr::core::NrPayloadRef;

    class NrSessionIoOperations final
    {
    public:
        NrSessionIoOperations(NrIocpOverlappedContextFactory& contextFactory,
                              internal::NrServerMetrics& metrics,
                              internal::NrDiagnosticEmitter diagnosticsEmitter) noexcept;

        NrSessionIoOperations(const NrSessionIoOperations&) = delete;
        NrSessionIoOperations& operator=(const NrSessionIoOperations&) = delete;

        [[nodiscard]] NrStatus PostRecv(NrSession& session) noexcept;
        [[nodiscard]] NrStatus PostSend(NrSession& session, NrPayloadRef payload) noexcept;
        [[nodiscard]] NrStatus RepostPendingSend(NrSession& session) noexcept;

    private:
        [[nodiscard]] NrStatus RecordRecvPostFailure(const NrSession& session, NrStatus status) const noexcept;
        [[nodiscard]] NrStatus RecordSendPostFailure(const NrSession& session, NrStatus status) const noexcept;
        void EmitPostFailure(internal::NrDiagnosticIoOperation ioOperation, const NrSession& session,
                             const NrStatus& status) const noexcept;

        NrIocpOverlappedContextFactory& contextFactory_;
        internal::NrServerMetrics& metrics_;
        internal::NrDiagnosticEmitter diagnosticsEmitter_;
    };
} // namespace psnr::runtime
