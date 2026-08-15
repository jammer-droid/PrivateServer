#pragma once

#include "NrIocpIoContextHeader.h"
#include "NrMemoryPool.h"
#include "NrSessionKey.h"
#include "NrWindows.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::runtime
{
    using psnr::core::NrPooledMemoryBlock;
    using psnr::core::NrSessionKey;

    struct NrRecvIoContext final
    {
        NrRecvIoContext(NrSessionKey ownerSessionKey, std::span<std::byte> writableBuffer) noexcept;

        [[nodiscard]] static NrRecvIoContext* FromOverlapped(OVERLAPPED* overlapped) noexcept;

        NrIocpIoContextHeader header;
        NrSessionKey sessionKey = 0;
        WSABUF wsabuf{};
        std::uint32_t writableLength = 0;
    };

    static_assert(offsetof(NrRecvIoContext, header) == 0);

    class NrRecvIoContextLease final
    {
    public:
        NrRecvIoContextLease() noexcept = default;

        NrRecvIoContextLease(const NrRecvIoContextLease&) = delete;
        NrRecvIoContextLease& operator=(const NrRecvIoContextLease&) = delete;

        NrRecvIoContextLease(NrRecvIoContextLease&& other) noexcept;
        NrRecvIoContextLease& operator=(NrRecvIoContextLease&& other) noexcept;

        ~NrRecvIoContextLease() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const void* ContextToken() const noexcept;
        [[nodiscard]] NrRecvIoContext& Context() noexcept;
        [[nodiscard]] const NrRecvIoContext& Context() const noexcept;
        [[nodiscard]] OVERLAPPED* Overlapped() noexcept;

        void Reset() noexcept;

    private:
        friend class NrIocpOverlappedContextFactory;

        explicit NrRecvIoContextLease(NrPooledMemoryBlock contextBlock) noexcept;

        [[nodiscard]] static NrRecvIoContext* ContextFromBlock(NrPooledMemoryBlock& contextBlock) noexcept;
        [[nodiscard]] static const NrRecvIoContext* ContextFromBlock(const NrPooledMemoryBlock& contextBlock) noexcept;

        NrPooledMemoryBlock contextBlock_;
    };
} // namespace psnr::runtime
