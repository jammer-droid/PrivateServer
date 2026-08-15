#include "pch.h"

#include "NrWinsockRuntime.h"

#include "NrErrorCode.h"
#include "NrWindows.h"

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrWinsockRuntime::~NrWinsockRuntime() noexcept
    {
        (void)Shutdown();
    }

    NrStatus NrWinsockRuntime::Configure(NrBootstrapContext&) noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrWinsockRuntime::Start() noexcept
    {
        if (started_)
        {
            return NrStatus::Success();
        }

        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data); // winsock version 2.2
        if (result != 0)
        {
            return NrStatus::Failure(NrErrorCode::IoFailed, static_cast<psnr::core::NrNativeErrorCode>(result));
        }

        started_ = true;
        return NrStatus::Success();
    }

    NrStatus NrWinsockRuntime::RequestStop(const NrStopContext&) noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrWinsockRuntime::Shutdown() noexcept
    {
        if (started_)
        {
            WSACleanup();
            started_ = false;
        }

        return NrStatus::Success();
    }
} // namespace psnr::runtime
