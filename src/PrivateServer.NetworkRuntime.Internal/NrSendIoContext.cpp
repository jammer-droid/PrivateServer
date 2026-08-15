#include "pch.h"

#include "NrSendIoContext.h"

#include "NrErrorCode.h"

#include <cassert>
#include <memory>
#include <span>
#include <utility>

namespace psnr::runtime
{
    NrSendIoContext::NrSendIoContext(NrSessionKey ownerSessionKey, NrPayloadRef ownerPayloadRef) noexcept
        : sessionKey(ownerSessionKey)
        , payloadRef(std::move(ownerPayloadRef))
        , payloadLength(static_cast<std::uint32_t>(payloadRef.Length()))
    {
        header.Reset(NrIoOperationType::Send);
        RefreshWsaBuffer();
    }

    NrSendIoContext* NrSendIoContext::FromOverlapped(OVERLAPPED* overlapped) noexcept
    {
        if (NrIocpIoContextHeader::FromOverlapped(overlapped) == nullptr)
        {
            return nullptr;
        }

        return reinterpret_cast<NrSendIoContext*>(overlapped);
    }

    std::uint32_t NrSendIoContext::RemainingBytes() const noexcept
    {
        return payloadLength - bytesSent;
    }

    bool NrSendIoContext::IsFullySent() const noexcept
    {
        return bytesSent == payloadLength;
    }

    NrStatus NrSendIoContext::AdvanceBytesSent(std::uint32_t bytesTransferred) noexcept
    {
        if (bytesTransferred == 0 || bytesTransferred > RemainingBytes())
        {
            return NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }

        bytesSent += bytesTransferred;
        RefreshWsaBuffer();
        return NrStatus::Success();
    }

    void NrSendIoContext::RefreshWsaBuffer() noexcept
    {
        const std::span<const std::byte> payloadBytes = payloadRef.Bytes();
        const std::uint32_t remainingBytes = RemainingBytes();

        if (remainingBytes == 0)
        {
            wsabuf.buf = nullptr;
            wsabuf.len = 0;
            return;
        }

        wsabuf.buf = reinterpret_cast<char*>(const_cast<std::byte*>(payloadBytes.data() + bytesSent));
        wsabuf.len = remainingBytes;
    }

    NrSendIoContextLease::NrSendIoContextLease(NrSendIoContextLease&& other) noexcept
        : contextBlock_(std::move(other.contextBlock_))
    {
    }

    NrSendIoContextLease& NrSendIoContextLease::operator=(NrSendIoContextLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            contextBlock_ = std::move(other.contextBlock_);
        }

        return *this;
    }

    NrSendIoContextLease::~NrSendIoContextLease() noexcept
    {
        Reset();
    }

    bool NrSendIoContextLease::IsValid() const noexcept
    {
        return contextBlock_.IsValid();
    }

    const void* NrSendIoContextLease::ContextToken() const noexcept
    {
        return IsValid() ? ContextFromBlock(contextBlock_) : nullptr;
    }

    NrSendIoContext& NrSendIoContextLease::Context() noexcept
    {
        assert(IsValid());
        return *ContextFromBlock(contextBlock_);
    }

    const NrSendIoContext& NrSendIoContextLease::Context() const noexcept
    {
        assert(IsValid());
        return *ContextFromBlock(contextBlock_);
    }

    OVERLAPPED* NrSendIoContextLease::Overlapped() noexcept
    {
        return IsValid() ? Context().header.Overlapped() : nullptr;
    }

    void NrSendIoContextLease::Reset() noexcept
    {
        if (IsValid())
        {
            std::destroy_at(ContextFromBlock(contextBlock_));
        }

        static_cast<void>(contextBlock_.Reset());
    }

    NrSendIoContextLease::NrSendIoContextLease(NrPooledMemoryBlock contextBlock) noexcept
        : contextBlock_(std::move(contextBlock))
    {
    }

    NrSendIoContext* NrSendIoContextLease::ContextFromBlock(NrPooledMemoryBlock& contextBlock) noexcept
    {
        return reinterpret_cast<NrSendIoContext*>(contextBlock.Data());
    }

    const NrSendIoContext* NrSendIoContextLease::ContextFromBlock(const NrPooledMemoryBlock& contextBlock) noexcept
    {
        return reinterpret_cast<const NrSendIoContext*>(contextBlock.Data());
    }
} // namespace psnr::runtime
