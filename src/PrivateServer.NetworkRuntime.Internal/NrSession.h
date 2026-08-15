#pragma once

#include "NrMemoryPoolManager.h"
#include "NrRecvBuffer.h"
#include "NrRecvIoContext.h"
#include "NrResult.h"
#include "NrSendIoContext.h"
#include "NrSessionKey.h"
#include "NrWin32Socket.h"

#include <cstddef>

namespace psnr::runtime
{
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrRecvBuffer;
    using psnr::core::NrResult;
    using psnr::core::NrSessionKey;

    class NrSession final
    {
    public:
        NrSession(const NrSession&) = delete;
        NrSession& operator=(const NrSession&) = delete;

        // move only
        NrSession(NrSession&& other) noexcept;
        NrSession& operator=(NrSession&& other) noexcept;

        ~NrSession() noexcept = default;

        [[nodiscard]] static NrResult<NrSession> Create(NrSessionKey sessionKey, NrWin32Socket&& acceptedSocket,
                                                        NrMemoryPoolManager& memoryPoolManager,
                                                        std::size_t recvBufferCapacity) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] NrSessionKey SessionKey() const noexcept;

        [[nodiscard]] NrWin32Socket& Socket() noexcept;
        [[nodiscard]] const NrWin32Socket& Socket() const noexcept;

        [[nodiscard]] NrRecvBuffer& RecvBuffer() noexcept;
        [[nodiscard]] const NrRecvBuffer& RecvBuffer() const noexcept;

        [[nodiscard]] bool HasPendingRecv() const noexcept;
        [[nodiscard]] NrRecvIoContextLease& PendingRecv() noexcept;
        [[nodiscard]] const NrRecvIoContextLease& PendingRecv() const noexcept;
        [[nodiscard]] NrStatus SetPendingRecv(NrRecvIoContextLease&& pendingRecv) noexcept;
        void ClearPendingRecv() noexcept;

        [[nodiscard]] bool HasPendingSend() const noexcept;
        [[nodiscard]] NrSendIoContextLease& PendingSend() noexcept;
        [[nodiscard]] const NrSendIoContextLease& PendingSend() const noexcept;
        [[nodiscard]] NrStatus SetPendingSend(NrSendIoContextLease&& pendingSend) noexcept;
        void ClearPendingSend() noexcept;

    private:
        NrSession(NrSessionKey sessionKey, NrWin32Socket&& acceptedSocket, NrRecvBuffer&& recvBuffer) noexcept;

        NrSessionKey sessionKey_ = 0;
        NrWin32Socket socket_;
        NrRecvBuffer recvBuffer_;

        // recv completion context
        NrRecvIoContextLease pendingRecv_;

        // send completion context
        NrSendIoContextLease pendingSend_;
    };
} // namespace psnr::runtime
