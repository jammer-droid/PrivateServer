#include "pch.h"

#include "NrIocpRuntime.h"

namespace psnr::runtime
{
    NrIocpRuntime::~NrIocpRuntime() noexcept
    {
        (void)Shutdown();
    }

    NrStatus NrIocpRuntime::Configure(NrBootstrapContext&) noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrIocpRuntime::Start() noexcept
    {
        return port_.Create();
    }

    NrStatus NrIocpRuntime::RequestStop(const NrStopContext&) noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrIocpRuntime::Shutdown() noexcept
    {
        return port_.Close();
    }

    NrIocpPort& NrIocpRuntime::Port() noexcept
    {
        return port_;
    }
} // namespace psnr::runtime
