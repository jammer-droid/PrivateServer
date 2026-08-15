#pragma once

#include "NrIocpCompletionPacket.h"
#include "NrStatus.h"
#include "NrWindows.h"

#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    class NrWin32Socket;

    class NrIocpPort final
    {
    public:
        NrIocpPort() noexcept = default;

        NrIocpPort(const NrIocpPort&) = delete;
        NrIocpPort& operator=(const NrIocpPort&) = delete;

        NrIocpPort(NrIocpPort&& other) noexcept;
        NrIocpPort& operator=(NrIocpPort&& other) noexcept;

        ~NrIocpPort() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] NrStatus Create() noexcept;
        [[nodiscard]] NrStatus AssociateSocket(const NrWin32Socket& socket, std::uintptr_t completionKey) noexcept;
        [[nodiscard]] NrStatus WaitForCompletion(NrIocpCompletionPacket& outPacket) noexcept;
        [[nodiscard]] NrStatus PostControlCompletion(std::uintptr_t completionKey = 0) noexcept;
        [[nodiscard]] NrStatus Close() noexcept;

    private:
        HANDLE port_ = nullptr;
    };
} // namespace psnr::runtime
