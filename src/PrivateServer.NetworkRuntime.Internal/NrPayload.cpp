#include "pch.h"

#include "NrPayload.h"

#include <algorithm>
#include <utility>

namespace psnr::core
{
    NrPayload::NrPayload(NrPayload&& other) noexcept
        : block_(std::move(other.block_))
        , length_(std::exchange(other.length_, 0))
    {
    }

    NrPayload& NrPayload::operator=(NrPayload&& other) noexcept
    {
        if (this != &other)
        {
            block_ = std::move(other.block_);
            length_ = std::exchange(other.length_, 0);
        }

        return *this;
    }

    NrPayload::~NrPayload() noexcept = default;

    NrPayload::NrPayload(NrPooledMemoryBlock block, const std::size_t length) noexcept
        : block_(std::move(block))
        , length_(length)
    {
    }

    std::span<std::byte> NrPayload::WritableBytes() noexcept
    {
        return std::span<std::byte>(block_.Data(), length_);
    }

    NrResult<NrPayload> NrPayloadFactory::CreatePayloadFrom(NrMemoryPoolManager& memoryPoolManager,
                                                            std::span<const std::byte> payloadBytes) noexcept
    {
        if (payloadBytes.empty()) // zero payload
        {
            return NrResult<NrPayload>(NrPayload{});
        }

        NrMemoryPoolRole role = NrMemoryPoolRole::Count;
        if (!TrySelectPayloadRole(payloadBytes.size(), role))
        {
            return NrResult<NrPayload>::Failure(NrErrorCode::CapacityExceeded);
        }

        NrResult<NrPooledMemoryBlock> blockResult = memoryPoolManager.AcquireBlock(role);
        if (blockResult.Failed())
        {
            return NrResult<NrPayload>::Failure(blockResult.Status());
        }

        NrPooledMemoryBlock block = blockResult.TakeValue();
        if (block.Capacity() < payloadBytes.size())
        {
            return NrResult<NrPayload>::Failure(NrErrorCode::CapacityExceeded);
        }

        NrPayload payload(std::move(block), payloadBytes.size());
        std::copy(payloadBytes.begin(), payloadBytes.end(), payload.WritableBytes().begin());

        return NrResult<NrPayload>(std::move(payload));
    }

    bool NrPayloadFactory::TrySelectPayloadRole(const std::size_t payloadLength, NrMemoryPoolRole& role) noexcept
    {
        if (payloadLength == 0) // zero payload는 먼저 필터링되어야 함
        {
            role = NrMemoryPoolRole::Count;
            return false;
        }

        if (payloadLength <= 64) // [1, 64]
        {
            role = NrMemoryPoolRole::Payload64;
            return true;
        }

        if (payloadLength <= 256) // [65, 256]
        {
            role = NrMemoryPoolRole::Payload256;
            return true;
        }

        if (payloadLength <= 1024) // [257, 1024]
        {
            role = NrMemoryPoolRole::Payload1024;
            return true;
        }

        if (payloadLength <= 8192) // [1025, 8192]
        {
            role = NrMemoryPoolRole::Payload8192;
            return true;
        }

        return false;
    }
} // namespace psnr::core
