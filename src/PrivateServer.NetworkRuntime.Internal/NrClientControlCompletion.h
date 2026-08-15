#pragma once

#include "NrIocpCompletionPacket.h"
#include "NrIocpPort.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    enum class NrClientControlCompletionKind : std::uintptr_t
    {
        None = 0,
        EventSpaceAvailable = 1,
        Stop = 2,
        CommandsReady = 3,
    };

    [[nodiscard]] psnr::core::NrStatus PostClientControlCompletion(NrIocpPort& iocpPort,
                                                                   NrClientControlCompletionKind kind) noexcept;

    [[nodiscard]] psnr::core::NrStatus DecodeClientControlCompletion(const NrIocpCompletionPacket& packet,
                                                                     NrClientControlCompletionKind& outKind) noexcept;
} // namespace psnr::runtime::internal
