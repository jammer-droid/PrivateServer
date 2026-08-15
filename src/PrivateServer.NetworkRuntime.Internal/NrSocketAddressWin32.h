#pragma once

#include "NrWindows.h"
#include "NrStatus.h"

#include "NrEndpoint.h"

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    struct NrSocketAddressWin32 final
    {
        sockaddr_in address{};
        int length = sizeof(sockaddr_in);
    };

    [[nodiscard]] NrStatus BuildSocketAddressWin32(const NrEndpoint& endpoint,
                                                   NrSocketAddressWin32& outAddress) noexcept;
} // namespace psnr::runtime
