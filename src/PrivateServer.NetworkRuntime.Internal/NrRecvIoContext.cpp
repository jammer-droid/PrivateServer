#include "pch.h"

#include "NrRecvIoContext.h"

#include <cassert>
#include <memory>
#include <utility>

namespace psnr::runtime
{
    NrRecvIoContext::NrRecvIoContext(NrSessionKey ownerSessionKey, std::span<std::byte> writableBuffer) noexcept
        : sessionKey(ownerSessionKey)
        , writableLength(static_cast<std::uint32_t>(writableBuffer.size()))
    {
        header.Reset(NrIoOperationType::Recv);
        wsabuf.buf = reinterpret_cast<char*>(writableBuffer.data());
        wsabuf.len = writableLength;
    }

    NrRecvIoContext* NrRecvIoContext::FromOverlapped(OVERLAPPED* overlapped) noexcept
    {
        if (NrIocpIoContextHeader::FromOverlapped(overlapped) == nullptr)
        {
            return nullptr;
        }

        return reinterpret_cast<NrRecvIoContext*>(overlapped);
    }

    NrRecvIoContextLease::NrRecvIoContextLease(NrRecvIoContextLease&& other) noexcept
        : contextBlock_(std::move(other.contextBlock_))
    {
    }

    NrRecvIoContextLease& NrRecvIoContextLease::operator=(NrRecvIoContextLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            contextBlock_ = std::move(other.contextBlock_);
        }

        return *this;
    }

    NrRecvIoContextLease::~NrRecvIoContextLease() noexcept
    {
        Reset();
    }

    bool NrRecvIoContextLease::IsValid() const noexcept
    {
        return contextBlock_.IsValid();
    }

    const void* NrRecvIoContextLease::ContextToken() const noexcept
    {
        return IsValid() ? ContextFromBlock(contextBlock_) : nullptr;
    }

    NrRecvIoContext& NrRecvIoContextLease::Context() noexcept
    {
        assert(IsValid());
        return *ContextFromBlock(contextBlock_);
    }

    const NrRecvIoContext& NrRecvIoContextLease::Context() const noexcept
    {
        assert(IsValid());
        return *ContextFromBlock(contextBlock_);
    }

    OVERLAPPED* NrRecvIoContextLease::Overlapped() noexcept
    {
        return IsValid() ? Context().header.Overlapped() : nullptr;
    }

    void NrRecvIoContextLease::Reset() noexcept
    {
        if (IsValid())
        {
            std::destroy_at(ContextFromBlock(contextBlock_));
        }

        static_cast<void>(contextBlock_.Reset());
    }

    NrRecvIoContextLease::NrRecvIoContextLease(NrPooledMemoryBlock contextBlock) noexcept
        : contextBlock_(std::move(contextBlock))
    {
    }

    NrRecvIoContext* NrRecvIoContextLease::ContextFromBlock(NrPooledMemoryBlock& contextBlock) noexcept
    {
        return reinterpret_cast<NrRecvIoContext*>(contextBlock.Data());
    }

    const NrRecvIoContext* NrRecvIoContextLease::ContextFromBlock(const NrPooledMemoryBlock& contextBlock) noexcept
    {
        return reinterpret_cast<const NrRecvIoContext*>(contextBlock.Data());
    }
} // namespace psnr::runtime
