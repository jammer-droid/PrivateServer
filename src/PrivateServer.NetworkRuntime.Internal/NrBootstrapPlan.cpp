#include "pch.h"

#include "NrLifecycleInternal.h"

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrBootstrapPlan::NrBootstrapPlan(const NrBootstrapPlanInput input) noexcept
        : components_(input.components)
        , diagnosticsEmitter_(input.diagnosticsEmitter)
    {
    }

    NrStatus NrBootstrapPlan::Configure(NrBootstrapContext& context) noexcept
    {
        for (INrServerLifecycleComponent* component : components_)
        {
            if (component == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            const NrStatus status = component->Configure(context);

            if (status.Failed())
            {
                return status;
            }
        }

        return NrStatus::Success();
    }

    NrStatus NrBootstrapPlan::Start() noexcept
    {
        startedCount_ = 0;

        for (INrServerLifecycleComponent* component : components_)
        {
            if (component == nullptr)
            {
                EmitFailure(internal::NrDiagnosticOperation::Start,
                            NrStatus::Failure(NrErrorCode::InvalidArgument));
                (void)ShutdownStarted();
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            const NrStatus status = component->Start();

            if (status.Failed())
            {
                EmitFailure(internal::NrDiagnosticOperation::Start, status);
                (void)ShutdownStarted();
                return status;
            }

            ++startedCount_;
        }

        return NrStatus::Success();
    }

    NrStatus NrBootstrapPlan::RequestStop(const NrStopContext& context) noexcept
    {
        NrStatus result = NrStatus::Success();

        for (std::size_t index = startedCount_; index > 0; --index)
        {
            const NrStatus status = components_[index - 1]->RequestStop(context);
            if (status.Failed())
            {
                EmitFailure(internal::NrDiagnosticOperation::RequestStop, status);
            }
            if (result.Succeeded() && status.Failed())
            {
                result = status;
            }
        }

        return result;
    }

    NrStatus NrBootstrapPlan::Shutdown() noexcept
    {
        return ShutdownStarted();
    }

    NrStatus NrBootstrapPlan::ShutdownStarted() noexcept
    {
        NrStatus result = NrStatus::Success();

        while (startedCount_ > 0)
        {
            --startedCount_;

            const NrStatus status = components_[startedCount_]->Shutdown();

            if (status.Failed())
            {
                EmitFailure(internal::NrDiagnosticOperation::Shutdown, status);
            }

            if (result.Succeeded() && status.Failed()) // 첫 실패를 반환
            {
                result = status;
            }
        }

        return result;
    }

    void NrBootstrapPlan::EmitFailure(const internal::NrDiagnosticOperation operation,
                                      const NrStatus& status) const noexcept
    {
        internal::NrDiagnosticRecord record;
        record.severity = internal::NrDiagnosticSeverity::Error;
        record.eventKind = internal::NrDiagnosticEventKind::Failure;
        record.component = internal::NrDiagnosticComponent::ServerLifecycle;
        record.operation = operation;
        record.errorCode = status.ErrorCode();
        record.nativeErrorCode = status.NativeErrorCode();
        diagnosticsEmitter_.Emit(record);
    }
} // namespace psnr::runtime
