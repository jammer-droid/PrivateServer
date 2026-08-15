#pragma once

#include "NrMemoryPoolManager.h"
#include "NrResult.h"

#include <cstddef>
#include <span>

namespace psnr::core
{
    class NrPayloadFactory;

    class NrPayload
    {
    public:
        NrPayload() noexcept = default;

        NrPayload(const NrPayload&) = delete;
        NrPayload& operator=(const NrPayload&) = delete;

        NrPayload(NrPayload&& other) noexcept;
        NrPayload& operator=(NrPayload&& other) noexcept;
        ~NrPayload() noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return length_ == 0;
        }

        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
        {
            return std::span<const std::byte>(block_.Data(), length_);
        }

        [[nodiscard]] std::size_t Length() const noexcept
        {
            return length_;
        }

        [[nodiscard]] std::size_t Capacity() const noexcept
        {
            return block_.Capacity();
        }

    private:
        friend class NrPayloadFactory;

        NrPayload(NrPooledMemoryBlock block, std::size_t length) noexcept;

        [[nodiscard]] std::span<std::byte> WritableBytes() noexcept; // only use in PayloadFactory

    private:
        NrPooledMemoryBlock block_;
        std::size_t length_ = 0;
    };

    class NrPayloadFactory
    {
    public:
        [[nodiscard]] static NrResult<NrPayload> CreatePayloadFrom(
            NrMemoryPoolManager& memoryPoolManager, std::span<const std::byte> payloadBytes) noexcept;

    private:
        [[nodiscard]] static bool TrySelectPayloadRole(std::size_t payloadLength, NrMemoryPoolRole& role) noexcept;
    };

} // namespace psnr::core
