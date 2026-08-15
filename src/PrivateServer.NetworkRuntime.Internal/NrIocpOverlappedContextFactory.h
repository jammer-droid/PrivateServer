#pragma once

#include "NrResult.h"
#include "NrSessionKey.h"

#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrPayloadRef.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::core
{
    class NrMemoryPoolManager;
}

namespace psnr::runtime
{
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrPayloadRef;
    using psnr::core::NrResult;
    using psnr::core::NrSessionKey;

    class NrIocpOverlappedContextFactory final
    {
    public:
        explicit NrIocpOverlappedContextFactory(NrMemoryPoolManager& memoryPoolManager) noexcept;

        NrIocpOverlappedContextFactory(const NrIocpOverlappedContextFactory&) = delete;
        NrIocpOverlappedContextFactory& operator=(const NrIocpOverlappedContextFactory&) = delete;

    public:
        // Context creation alone does not make native I/O pending.
        [[nodiscard]] NrResult<NrRecvIoContextLease> CreateRecv(NrSessionKey sessionKey,
                                                                std::span<std::byte> writableBuffer) noexcept;

        [[nodiscard]] NrResult<NrSendIoContextLease> CreateSendContext(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] NrResult<NrSendIoContextLease> CreateSend(NrSessionKey sessionKey,
                                                                std::span<const std::byte> payload) noexcept;
        [[nodiscard]] NrResult<NrSendIoContextLease> CreateSend(NrSessionKey sessionKey, NrPayloadRef payload) noexcept;

    private:
        NrMemoryPoolManager& memoryPoolManager_;
    };
} // namespace psnr::runtime
