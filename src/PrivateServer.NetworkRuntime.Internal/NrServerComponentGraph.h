#pragma once

#include "NrActorScheduleHandle.h"
#include "NrServerLifecycleOrder.h"
#include "NrServerMetrics.h"
#include "NrServerSubmissionGate.h"
#include "NrSessionEndReason.h"
#include "NrSessionKey.h"
#include "NrStatus.h"
#include "NrToWorldHandoff.h"

#include <memory>
#include <span>
#include <vector>

namespace psnr::core
{
    class NrMemoryPoolManager;
    struct NrMemoryPoolManagerConfig;
}

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    class NrSessionActorRegistry;

    namespace internal
    {
        class NrDiagnosticEmitter;
        class NrDiagnosticsComponent;
        struct NrDiagnosticsStats;
    }

    class NrServerComponentGraph final
    {
    public:
        NrServerComponentGraph() noexcept = default;

        NrServerComponentGraph(const NrServerComponentGraph&) = delete;
        NrServerComponentGraph& operator=(const NrServerComponentGraph&) = delete;

        NrServerComponentGraph(NrServerComponentGraph&&) noexcept;
        NrServerComponentGraph& operator=(NrServerComponentGraph&&) = delete;

        ~NrServerComponentGraph() noexcept;

        [[nodiscard]] NrStatus AddOwnedComponent(std::unique_ptr<INrServerLifecycleComponent> component) noexcept;

        [[nodiscard]] NrStatus InitializeMetrics() noexcept;
        [[nodiscard]] internal::NrServerMetrics* Metrics() noexcept;
        [[nodiscard]] const internal::NrServerMetrics* Metrics() const noexcept;

        [[nodiscard]] NrStatus InitializeMemoryPoolManager(
            const psnr::core::NrMemoryPoolManagerConfig& config) noexcept;
        [[nodiscard]] psnr::core::NrMemoryPoolManager* MemoryPoolManager() noexcept;
        [[nodiscard]] const psnr::core::NrMemoryPoolManager* MemoryPoolManager() const noexcept;

        [[nodiscard]] NrStatus InitializeSubmissionGate() noexcept;
        [[nodiscard]] internal::NrSubmissionAdmissionHandle SubmissionAdmissionHandle() const noexcept;
        [[nodiscard]] NrStatus InvalidateSubmissionsAndWait() const noexcept;
        [[nodiscard]] NrStatus AttachToWorldHandoff(std::unique_ptr<internal::NrToWorldHandoff> handoff) noexcept;
        [[nodiscard]] internal::NrToWorldHandoff* ToWorldHandoff() noexcept;
        [[nodiscard]] const internal::NrToWorldHandoff* ToWorldHandoff() const noexcept;
        [[nodiscard]] NrStatus AttachSessionActorScheduleHandle(NrActorScheduleHandle handle) noexcept;
        [[nodiscard]] NrStatus AttachSessionActorRegistry(NrSessionActorRegistry& registry) noexcept;
        [[nodiscard]] const NrSessionActorRegistry* SessionActorRegistry() const noexcept;
        [[nodiscard]] NrStatus AttachDiagnosticsComponent(internal::NrDiagnosticsComponent& component) noexcept;
        [[nodiscard]] internal::NrDiagnosticEmitter DiagnosticsEmitter() noexcept;
        [[nodiscard]] internal::NrDiagnosticsStats CaptureDiagnosticsStats() const noexcept;
        [[nodiscard]] NrStatus RequestSessionClose(psnr::core::NrSessionKey sessionKey,
                                                   NrSessionEndReason endReason) const noexcept;

        [[nodiscard]] NrStatus RebuildLifecycleOrder() noexcept;

        [[nodiscard]] std::span<INrServerLifecycleComponent* const> LifecycleOrder() noexcept;
        [[nodiscard]] NrBootstrapPlanInput BootstrapPlanInput() noexcept;

    private:
        std::unique_ptr<internal::NrServerMetrics> metrics_;
        std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager_;
        std::unique_ptr<internal::NrToWorldHandoff> toWorldHandoff_;
        std::vector<std::unique_ptr<INrServerLifecycleComponent>> ownedComponents_;
        NrServerLifecycleOrder lifecycleOrder_;
        internal::NrServerSubmissionGate submissionGate_;
        NrSessionActorRegistry* sessionActorRegistry_ = nullptr; // non-owning, ownedComponents_가 lifetime을 보장한다.
        internal::NrDiagnosticsComponent* diagnosticsComponent_ = nullptr;
        NrActorScheduleHandle sessionActorScheduleHandle_;
    };
} // namespace psnr::runtime
