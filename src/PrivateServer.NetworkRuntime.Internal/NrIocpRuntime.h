#pragma once

#include "NrIocpPort.h"
#include "NrLifecycleInternal.h"

namespace psnr::runtime
{
    class NrIocpRuntime final : public INrServerLifecycleComponent
    {
    public:
        NrIocpRuntime() noexcept = default;
        ~NrIocpRuntime() noexcept override;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override;
        [[nodiscard]] NrStatus Start() noexcept override;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override;
        [[nodiscard]] NrStatus Shutdown() noexcept override;

        [[nodiscard]] NrIocpPort& Port() noexcept;

    private:
        NrIocpPort port_;
    };
} // namespace psnr::runtime
