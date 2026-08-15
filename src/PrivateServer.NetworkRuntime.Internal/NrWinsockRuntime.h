#pragma once

#include "NrLifecycleInternal.h"

namespace psnr::runtime
{
    class NrWinsockRuntime final : public INrServerLifecycleComponent
    {
    public:
        NrWinsockRuntime() noexcept = default;
        ~NrWinsockRuntime() noexcept override;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override;
        [[nodiscard]] NrStatus Start() noexcept override;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override;
        [[nodiscard]] NrStatus Shutdown() noexcept override;

    private:
        bool started_ = false;
    };
} // namespace psnr::runtime
