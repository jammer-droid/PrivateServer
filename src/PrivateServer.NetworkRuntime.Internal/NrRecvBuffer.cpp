#include "pch.h"

#include "NrRecvBuffer.h"

#include <cassert>
#include <cstring>
#include <utility>

namespace psnr::core
{
    NrRecvBuffer::NrRecvBuffer(NrRecvBuffer&& other) noexcept
        : storageBlock_(std::move(other.storageBlock_))
        , capacity_(std::exchange(other.capacity_, 0))
        , readPos_(std::exchange(other.readPos_, 0))
        , writePos_(std::exchange(other.writePos_, 0))
    {
    }

    NrRecvBuffer& NrRecvBuffer::operator=(NrRecvBuffer&& other) noexcept
    {
        if (this != &other)
        {
            storageBlock_ = std::move(other.storageBlock_);
            capacity_ = std::exchange(other.capacity_, 0);
            readPos_ = std::exchange(other.readPos_, 0);
            writePos_ = std::exchange(other.writePos_, 0);
        }

        return *this;
    }

    NrRecvBuffer::~NrRecvBuffer() noexcept = default;

    NrResult<NrRecvBuffer> NrRecvBuffer::Create(NrMemoryPoolManager& memoryPoolManager,
                                                const std::size_t capacity) noexcept
    {
        if (capacity == 0)
        {
            return NrResult<NrRecvBuffer>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<NrPooledMemoryBlock> blockResult = memoryPoolManager.AcquireBlock(NrMemoryPoolRole::RecvBuffer);
        if (blockResult.Failed())
        {
            return NrResult<NrRecvBuffer>::Failure(blockResult.Status());
        }

        NrPooledMemoryBlock block = blockResult.TakeValue();
        if (!block.IsValid() || block.Data() == nullptr)
        {
            return NrResult<NrRecvBuffer>::Failure(NrErrorCode::InvalidState);
        }

        if (block.Capacity() < capacity)
        {
            return NrResult<NrRecvBuffer>::Failure(NrErrorCode::CapacityExceeded);
        }

        return NrResult<NrRecvBuffer>(NrRecvBuffer(std::move(block), capacity));
    }

    std::span<std::byte> NrRecvBuffer::WritableSpan() noexcept
    {
        std::byte* const data = storageBlock_.Data();
        if (data == nullptr)
        {
            return {};
        }

        return std::span<std::byte>(data + writePos_, WritableBytes());
    }

    std::span<const std::byte> NrRecvBuffer::ReadableSpan() const noexcept
    {
        const std::byte* const data = storageBlock_.Data();
        if (data == nullptr)
        {
            return {};
        }

        return std::span<const std::byte>(data + readPos_, ReadableBytes());
    }

    NrStatus NrRecvBuffer::CommitWritten(const std::size_t bytesTransferred) noexcept
    {
        if (bytesTransferred > WritableBytes())
        {
            return NrStatus(NrErrorCode::InvalidArgument);
        }

        writePos_ += bytesTransferred;
        return NrStatus();
    }

    NrStatus NrRecvBuffer::Consume(const std::size_t bytes) noexcept
    {
        if (bytes > ReadableBytes())
        {
            return NrStatus(NrErrorCode::InvalidArgument);
        }

        readPos_ += bytes;
        return NrStatus();
    }

    void NrRecvBuffer::Compact() noexcept
    {
        assert(readPos_ <= writePos_);
        assert(writePos_ <= capacity_);
        assert(capacity_ == 0 || storageBlock_.Data() != nullptr);

        const std::size_t readableBytes = ReadableBytes();
        if (readableBytes > 0 && readPos_ > 0)
        {
            std::byte* const data = storageBlock_.Data();
            std::memmove(data, data + readPos_, readableBytes);
        }

        readPos_ = 0;
        writePos_ = readableBytes;
    }

    NrRecvBuffer::NrRecvBuffer(NrPooledMemoryBlock storageBlock, const std::size_t capacity) noexcept
        : storageBlock_(std::move(storageBlock))
        , capacity_(capacity)
    {
    }

} // namespace psnr::core
