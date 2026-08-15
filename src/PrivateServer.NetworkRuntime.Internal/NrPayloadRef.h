#pragma once

#include "NrMemoryPoolManager.h"
#include "NrPacketType.h"
#include "NrResult.h"

#include <cstddef>
#include <span>

namespace psnr::core
{
    struct NrPayloadRefControlBlock;

    class NrPayloadRef // move-only
    {
    public:
        NrPayloadRef() noexcept = default;

        NrPayloadRef(const NrPayloadRef&) = delete;
        NrPayloadRef& operator=(const NrPayloadRef&) = delete;

        NrPayloadRef(NrPayloadRef&& other) noexcept;
        NrPayloadRef& operator=(NrPayloadRef&& other) noexcept;

        ~NrPayloadRef() noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return control_ == nullptr;
        }

        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
        [[nodiscard]] std::size_t Length() const noexcept;
        [[nodiscard]] NrPayloadRef Share() const noexcept;

    private:
        friend class NrPayloadRefFactory;

        explicit NrPayloadRef(NrPayloadRefControlBlock* control) noexcept;

        void Reset() noexcept;

    private:
        NrPayloadRefControlBlock* control_ = nullptr;
    };

    class NrPayloadRefFactory
    {
    public:
        [[nodiscard]] static NrResult<NrPayloadRef> CreatePayloadRefFrom(
            NrMemoryPoolManager& memoryPoolManager, std::span<const std::byte> payloadBytes) noexcept;
        [[nodiscard]] static NrResult<NrPayloadRef> CreateFramedPayloadRef(
            NrMemoryPoolManager& memoryPoolManager, NrPacketType packetType,
            std::span<const std::byte> semanticPayloadBytes) noexcept;

    private:
        [[nodiscard]] static NrResult<NrPayloadRef> CreateFromOwnedBlock(NrMemoryPoolManager& memoryPoolManager,
                                                                         NrPooledMemoryBlock payloadBlock,
                                                                         std::size_t payloadLength) noexcept;
        [[nodiscard]] static bool TrySelectPayloadRole(std::size_t payloadLength, NrMemoryPoolRole& role) noexcept;
    };

} // namespace psnr::core
