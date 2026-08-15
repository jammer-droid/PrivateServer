#pragma once

#include "NrAcceptIoContext.h"
#include "NrEndpoint.h"
#include "NrLifecycleInternal.h"
#include "NrSessionKey.h"
#include "NrWin32Socket.h"

#include <cstdint>
#include <vector>

namespace psnr::runtime
{
    using psnr::core::NrSessionKey;

    class NrIocpPort;

    struct NrListenerConfig final
    {
        constexpr NrListenerConfig(NrEndpoint endpoint, int backlog, std::uint32_t acceptSlots) noexcept
            : bindEndpoint(endpoint)
            , listenBacklog(backlog)
            , acceptSlotCount(acceptSlots)
        {
        }

        NrEndpoint bindEndpoint;
        int listenBacklog;
        std::uint32_t acceptSlotCount;
    };

    struct NrListenerDependencies final
    {
        NrIocpPort* iocpPort = nullptr;
        internal::NrDiagnosticEmitter diagnosticsEmitter;
    };

    class NrListener final : public INrServerLifecycleComponent
    {
    public:
        NrListener(NrListenerConfig config, NrListenerDependencies dependencies) noexcept;
        ~NrListener() noexcept override;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override;
        [[nodiscard]] NrStatus Start() noexcept override;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override;
        [[nodiscard]] NrStatus Shutdown() noexcept override;

        [[nodiscard]] NrStatus CompleteAccept(NrAcceptIoContext& acceptContext, NrSessionKey sessionKey) noexcept;
        [[nodiscard]] NrStatus PostAccept(NrAcceptIoContext& acceptContext) noexcept;

    private:
        void EmitPostFailure(const NrStatus& status) const noexcept;

        NrListenerConfig config_;
        NrListenerDependencies dependencies_;
        NrWin32Socket listenSocket_;
        std::vector<NrAcceptIoContext> acceptContexts_;
    };
} // namespace psnr::runtime
