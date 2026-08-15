#include "pch.h"

#include "NrServerComponentGraph.h"

#include "NrErrorCode.h"
#include "NrDiagnosticsComponent.h"
#include "NrMemoryPoolManager.h"
#include "NrSessionActorRegistry.h"

#include <new>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrServerComponentGraph::NrServerComponentGraph(NrServerComponentGraph&&) noexcept = default;

    NrServerComponentGraph::~NrServerComponentGraph() noexcept = default;

    NrStatus NrServerComponentGraph::AddOwnedComponent(std::unique_ptr<INrServerLifecycleComponent> component) noexcept
    {
        if (component == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        try
        {
            ownedComponents_.push_back(std::move(component));
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }

    NrStatus NrServerComponentGraph::InitializeMetrics() noexcept
    {
        if (metrics_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        metrics_.reset(new (std::nothrow) internal::NrServerMetrics());
        return metrics_ == nullptr ? NrStatus::Failure(NrErrorCode::OutOfMemory) : NrStatus::Success();
    }

    internal::NrServerMetrics* NrServerComponentGraph::Metrics() noexcept
    {
        return metrics_.get();
    }

    const internal::NrServerMetrics* NrServerComponentGraph::Metrics() const noexcept
    {
        return metrics_.get();
    }

    NrStatus NrServerComponentGraph::InitializeMemoryPoolManager(
        const psnr::core::NrMemoryPoolManagerConfig& config) noexcept
    {
        if (memoryPoolManager_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
            psnr::core::NrMemoryPoolManager::Create(config);
        if (managerResult.Failed())
        {
            return managerResult.Status();
        }

        memoryPoolManager_ = managerResult.TakeValue();
        return NrStatus::Success();
    }

    psnr::core::NrMemoryPoolManager* NrServerComponentGraph::MemoryPoolManager() noexcept
    {
        return memoryPoolManager_.get();
    }

    const psnr::core::NrMemoryPoolManager* NrServerComponentGraph::MemoryPoolManager() const noexcept
    {
        return memoryPoolManager_.get();
    }

    NrStatus NrServerComponentGraph::InitializeSubmissionGate() noexcept
    {
        if (submissionGate_.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        psnr::core::NrResult<internal::NrServerSubmissionGate> gateResult = internal::NrServerSubmissionGate::Create();
        if (gateResult.Failed())
        {
            return gateResult.Status();
        }

        submissionGate_ = gateResult.TakeValue();
        return NrStatus::Success();
    }

    internal::NrSubmissionAdmissionHandle NrServerComponentGraph::SubmissionAdmissionHandle() const noexcept
    {
        return submissionGate_.CreateAdmissionHandle();
    }

    NrStatus NrServerComponentGraph::InvalidateSubmissionsAndWait() const noexcept
    {
        return submissionGate_.InvalidateAndWait();
    }

    NrStatus NrServerComponentGraph::AttachToWorldHandoff(std::unique_ptr<internal::NrToWorldHandoff> handoff) noexcept
    {
        if (handoff == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
        if (toWorldHandoff_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        toWorldHandoff_ = std::move(handoff);
        return NrStatus::Success();
    }

    internal::NrToWorldHandoff* NrServerComponentGraph::ToWorldHandoff() noexcept
    {
        return toWorldHandoff_.get();
    }

    const internal::NrToWorldHandoff* NrServerComponentGraph::ToWorldHandoff() const noexcept
    {
        return toWorldHandoff_.get();
    }

    NrStatus NrServerComponentGraph::AttachSessionActorScheduleHandle(NrActorScheduleHandle handle) noexcept
    {
        if (!handle.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
        if (sessionActorScheduleHandle_.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        sessionActorScheduleHandle_ = handle;
        return NrStatus::Success();
    }

    NrStatus NrServerComponentGraph::AttachSessionActorRegistry(NrSessionActorRegistry& registry) noexcept
    {
        if (sessionActorRegistry_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        sessionActorRegistry_ = &registry;
        return NrStatus::Success();
    }

    const NrSessionActorRegistry* NrServerComponentGraph::SessionActorRegistry() const noexcept
    {
        return sessionActorRegistry_;
    }

    NrStatus NrServerComponentGraph::AttachDiagnosticsComponent(internal::NrDiagnosticsComponent& component) noexcept
    {
        if (diagnosticsComponent_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        diagnosticsComponent_ = &component;
        return NrStatus::Success();
    }

    internal::NrDiagnosticEmitter NrServerComponentGraph::DiagnosticsEmitter() noexcept
    {
        return diagnosticsComponent_ == nullptr ? internal::NrDiagnosticEmitter{} : diagnosticsComponent_->Emitter();
    }

    internal::NrDiagnosticsStats NrServerComponentGraph::CaptureDiagnosticsStats() const noexcept
    {
        return diagnosticsComponent_ == nullptr ? internal::NrDiagnosticsStats{}
                                                : diagnosticsComponent_->CaptureStats();
    }

    NrStatus NrServerComponentGraph::RequestSessionClose(const psnr::core::NrSessionKey sessionKey,
                                                         const NrSessionEndReason endReason) const noexcept
    {
        if (!sessionActorScheduleHandle_.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }
        if (sessionKey == 0 || endReason == NrSessionEndReason::None)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return sessionActorScheduleHandle_.Enqueue(
            sessionKey,
            psnr::core::NrSessionRecvEvent{psnr::core::NrSessionRecvEventType::CloseRequested, {}, endReason});
    }

    NrStatus NrServerComponentGraph::RebuildLifecycleOrder() noexcept
    {
        lifecycleOrder_.Clear();

        for (const std::unique_ptr<INrServerLifecycleComponent>& component : ownedComponents_)
        {
            const NrStatus status = lifecycleOrder_.Add(*component);
            if (status.Failed())
            {
                return status;
            }
        }

        return NrStatus::Success();
    }

    std::span<INrServerLifecycleComponent* const> NrServerComponentGraph::LifecycleOrder() noexcept
    {
        return lifecycleOrder_.Components();
    }

    NrBootstrapPlanInput NrServerComponentGraph::BootstrapPlanInput() noexcept
    {
        return NrBootstrapPlanInput{LifecycleOrder(), DiagnosticsEmitter()};
    }
} // namespace psnr::runtime
