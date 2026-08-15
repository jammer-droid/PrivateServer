#pragma once

#include "NrIocpIoContextHeader.h"
#include "NrIoOperationType.h"
#include "NrSocketAddressWin32.h"
#include "NrWindows.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    class NrClientConnectIoContext final
    {
    public:
        NrClientConnectIoContext(std::uint64_t attemptGeneration, const NrSocketAddressWin32& remoteAddress) noexcept;

        NrClientConnectIoContext(const NrClientConnectIoContext&) = delete;
        NrClientConnectIoContext& operator=(const NrClientConnectIoContext&) = delete;

        NrClientConnectIoContext(NrClientConnectIoContext&&) = delete;
        NrClientConnectIoContext& operator=(NrClientConnectIoContext&&) = delete;

        [[nodiscard]] static NrClientConnectIoContext* FromOverlapped(OVERLAPPED* overlapped) noexcept;

        [[nodiscard]] OVERLAPPED* Overlapped() noexcept;
        [[nodiscard]] NrIoOperationType Type() const noexcept;
        [[nodiscard]] std::uint64_t AttemptGeneration() const noexcept;
        [[nodiscard]] const sockaddr* RemoteAddress() const noexcept;
        [[nodiscard]] int RemoteAddressLength() const noexcept;

    private:
        // Must stay first. FromOverlapped() relies on the header starting at the context address.
        NrIocpIoContextHeader header_;
        std::uint64_t attemptGeneration_ = 0; // 연결 시도 구분
        NrSocketAddressWin32 remoteAddress_;  // remote server address
    };
} // namespace psnr::runtime::internal
