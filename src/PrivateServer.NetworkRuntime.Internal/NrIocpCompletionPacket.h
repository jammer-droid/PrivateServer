#pragma once

#include "NrStatus.h"
#include "NrWindows.h"

#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    struct NrIocpCompletionPacket final
    {
        DWORD bytesTransferred = 0;
        std::uintptr_t completionKey = 0;
        OVERLAPPED* overlapped = nullptr;
        NrStatus ioStatus = NrStatus::Success();
    };
} // namespace psnr::runtime
