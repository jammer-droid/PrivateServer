#pragma once

#include "NrIocpIoContextHeader.h"
#include "NrMemoryPool.h"
#include "NrPayloadRef.h"
#include "NrSessionKey.h"
#include "NrStatus.h"
#include "NrWindows.h"

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrPayloadRef;
    using psnr::core::NrPooledMemoryBlock;
    using psnr::core::NrSessionKey;
    using psnr::core::NrStatus;

    struct NrSendIoContext final
    {
        NrSendIoContext(NrSessionKey ownerSessionKey, NrPayloadRef ownerPayloadRef) noexcept;

        [[nodiscard]] static NrSendIoContext* FromOverlapped(OVERLAPPED* overlapped) noexcept;
        [[nodiscard]] std::uint32_t RemainingBytes() const noexcept;
        [[nodiscard]] bool IsFullySent() const noexcept;
        [[nodiscard]] NrStatus AdvanceBytesSent(std::uint32_t bytesTransferred) noexcept;
        void RefreshWsaBuffer() noexcept;

        NrIocpIoContextHeader header;
        NrSessionKey sessionKey = 0;
        WSABUF wsabuf{};
        NrPayloadRef payloadRef;
        std::uint32_t payloadLength = 0;
        std::uint32_t bytesSent = 0;
    };

    static_assert(offsetof(NrSendIoContext, header) == 0);

    class NrSendIoContextLease final
    {
    public:
        NrSendIoContextLease() noexcept = default;

        NrSendIoContextLease(const NrSendIoContextLease&) = delete;
        NrSendIoContextLease& operator=(const NrSendIoContextLease&) = delete;

        NrSendIoContextLease(NrSendIoContextLease&& other) noexcept;
        NrSendIoContextLease& operator=(NrSendIoContextLease&& other) noexcept;

        ~NrSendIoContextLease() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const void* ContextToken() const noexcept;
        [[nodiscard]] NrSendIoContext& Context() noexcept;
        [[nodiscard]] const NrSendIoContext& Context() const noexcept;
        [[nodiscard]] OVERLAPPED* Overlapped() noexcept;

        void Reset() noexcept;

    private:
        friend class NrIocpOverlappedContextFactory;

        explicit NrSendIoContextLease(NrPooledMemoryBlock contextBlock) noexcept;

        [[nodiscard]] static NrSendIoContext* ContextFromBlock(NrPooledMemoryBlock& contextBlock) noexcept;
        [[nodiscard]] static const NrSendIoContext* ContextFromBlock(const NrPooledMemoryBlock& contextBlock) noexcept;

        NrPooledMemoryBlock contextBlock_;
    };
} // namespace psnr::runtime
