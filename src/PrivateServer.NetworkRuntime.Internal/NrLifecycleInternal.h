#pragma once

// Lifecycle hook contract

#include "NrDiagnosticEmitter.h"
#include "NrStatus.h"

#include <cstddef>
#include <span>

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    enum class NrStopReason
    {
        Requested,
        BootstrapFailure,
        Shutdown,
    };

    enum class NrStopMode
    {
        Graceful,
        Forced,
    };

    class NrStopContext final
    {
    public:
        constexpr NrStopContext(NrStopReason reason = NrStopReason::Requested,
                                NrStopMode mode = NrStopMode::Graceful) noexcept
            : reason_(reason)
            , mode_(mode)
        {
        }

        [[nodiscard]] constexpr NrStopReason Reason() const noexcept
        {
            return reason_;
        }

        [[nodiscard]] constexpr NrStopMode Mode() const noexcept
        {
            return mode_;
        }

    private:
        NrStopReason reason_ = NrStopReason::Requested;
        NrStopMode mode_ = NrStopMode::Graceful;
    };

    class NrBootstrapContext;

    class INrServerLifecycleComponent
    {
    public:
        INrServerLifecycleComponent() noexcept = default;

        INrServerLifecycleComponent(const INrServerLifecycleComponent&) = delete;
        INrServerLifecycleComponent& operator=(const INrServerLifecycleComponent&) = delete;

        virtual ~INrServerLifecycleComponent() noexcept = default;

        [[nodiscard]] virtual NrStatus Configure(NrBootstrapContext& context) noexcept = 0;
        [[nodiscard]] virtual NrStatus Start() noexcept = 0;
        [[nodiscard]] virtual NrStatus RequestStop(const NrStopContext& context) noexcept = 0;
        [[nodiscard]] virtual NrStatus Shutdown() noexcept = 0;
    };

    class NrBootstrapContext final
    {
    public:
        constexpr NrBootstrapContext() noexcept = default;
    };

    struct NrBootstrapPlanInput final
    {
        std::span<INrServerLifecycleComponent* const> components;
        internal::NrDiagnosticEmitter diagnosticsEmitter;
    };

    class NrBootstrapPlan final
    {
    public:
        NrBootstrapPlan() noexcept = default;

        explicit NrBootstrapPlan(NrBootstrapPlanInput input) noexcept;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept;
        [[nodiscard]] NrStatus Start() noexcept;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept;
        [[nodiscard]] NrStatus Shutdown() noexcept;

    private:
        [[nodiscard]] NrStatus ShutdownStarted() noexcept;
        void EmitFailure(internal::NrDiagnosticOperation operation, const NrStatus& status) const noexcept;

        std::span<INrServerLifecycleComponent* const> components_{};
        internal::NrDiagnosticEmitter diagnosticsEmitter_{};
        std::size_t startedCount_ = 0;
    };

} // namespace psnr::runtime
