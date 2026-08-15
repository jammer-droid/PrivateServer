#pragma once

#include "NrAcceptIoContext.h"
#include "NrDiagnosticEmitter.h"
#include "NrIoEvent.h"
#include "NrSessionActorScheduler.h"
#include "NrServerSubmissionGate.h"
#include "NrSessionKey.h"
#include "NrStatus.h"

#include <cstddef>

namespace psnr::core
{
    class NrMemoryPoolManager;
    struct NrSessionRecvDrainDependencies;
} // namespace psnr::core

namespace psnr::runtime
{
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrSessionKey;
    using psnr::core::NrStatus;

    class NrListener;
    class NrSessionActorRegistry;
    class NrSessionIoOperations;
    class NrIoEventDispatcherTestAccess;
    struct NrRecvIoContext;
    struct NrSendIoContext;

    namespace internal
    {
        class NrToWorldHandoff;
    }

    struct NrIoEventDispatcherDependencies final
    {
        NrListener* listener = nullptr;
        NrSessionActorRegistry* sessionActorRegistry = nullptr;
        NrSessionIoOperations* sessionIoOperations = nullptr;
        internal::NrToWorldHandoff* toWorldHandoff = nullptr;
        NrActorScheduleHandle actorScheduleHandle;
        internal::NrSubmissionAdmissionHandle submissionAdmission;
        internal::NrDiagnosticEmitter diagnosticsEmitter;
        NrMemoryPoolManager* memoryPoolManager = nullptr;
        std::size_t recvBufferCapacity = 0;
        std::size_t actorMailboxCapacity = 0;
        std::size_t pendingSendQueueCapacity = 0;
        const psnr::core::NrSessionRecvDrainDependencies* recvDrainDependencies = nullptr;
    };

    class NrIoEventDispatcher final
    {
    public:
        NrIoEventDispatcher() noexcept = default;
        explicit NrIoEventDispatcher(NrIoEventDispatcherDependencies dependencies) noexcept;

        [[nodiscard]] NrStatus DispatchAccept(const NrIoEvent& event, NrAcceptIoContext& context) noexcept;
        [[nodiscard]] NrStatus DispatchRecv(const NrIoEvent& event, NrRecvIoContext& context) noexcept;
        [[nodiscard]] NrStatus DispatchSend(const NrIoEvent& event, NrSendIoContext& context) noexcept;

    private:
        friend class NrIoEventDispatcherTestAccess;

        [[nodiscard]] NrStatus ValidateAcceptDependencies() const noexcept;
        [[nodiscard]] NrStatus AllocateSessionKey(NrSessionKey& outSessionKey) noexcept;
        [[nodiscard]] NrStatus RecordSessionBootstrapFailure(NrSessionKey sessionKey,
                                                             const NrStatus& status) const noexcept;

        NrIoEventDispatcherDependencies dependencies_;
        NrSessionKey nextSessionKey_ = 1;
    };
} // namespace psnr::runtime
