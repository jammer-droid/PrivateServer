#pragma once

#include "NrMemoryPoolManager.h"
#include "NrResult.h"
#include "NrStatus.h"

#include <cstddef>
#include <span>

namespace psnr::core
{
    class NrRecvBuffer
    {
    public:
        NrRecvBuffer(const NrRecvBuffer&) = delete;
        NrRecvBuffer& operator=(const NrRecvBuffer&) = delete;

        NrRecvBuffer(NrRecvBuffer&& other) noexcept;
        NrRecvBuffer& operator=(NrRecvBuffer&& other) noexcept;

        ~NrRecvBuffer() noexcept;

        [[nodiscard]] static NrResult<NrRecvBuffer> Create(NrMemoryPoolManager& memoryPoolManager,
                                                           std::size_t capacity) noexcept;

        [[nodiscard]] std::span<std::byte> WritableSpan() noexcept;
        [[nodiscard]] std::span<const std::byte> ReadableSpan() const noexcept;

        [[nodiscard]] NrStatus CommitWritten(std::size_t bytesTransferred) noexcept;
        [[nodiscard]] NrStatus Consume(std::size_t bytes) noexcept;
        void Compact() noexcept;

        [[nodiscard]] std::size_t Capacity() const noexcept
        {
            return capacity_;
        }

        [[nodiscard]] std::size_t ReadableBytes() const noexcept
        {
            return writePos_ - readPos_;
        }

        [[nodiscard]] std::size_t WritableBytes() const noexcept
        {
            return capacity_ - writePos_;
        }

        [[nodiscard]] std::size_t ConsumedBytes() const noexcept
        {
            return readPos_;
        }

    private:
        NrRecvBuffer(NrPooledMemoryBlock storageBlock, std::size_t capacity) noexcept;

    private:
        NrPooledMemoryBlock storageBlock_;
        std::size_t capacity_ = 0;
        std::size_t readPos_ = 0;
        std::size_t writePos_ = 0;
    };

} // namespace psnr::core
