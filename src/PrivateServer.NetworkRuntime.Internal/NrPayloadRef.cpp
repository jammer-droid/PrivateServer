#include "pch.h"

#include "NrPayloadRef.h"

#include "NrMemoryPool.h"
#include "NrPacketHeaderCodec.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

namespace psnr::core
{
    struct NrPayloadRefControlBlock final
    {
        NrPayloadRefControlBlock(NrPooledMemoryBlock payloadBlock, std::size_t payloadLength,
                                 NrMemoryPool* ownerControlPool, std::byte* ownerControlMemory) noexcept
            : length(payloadLength)
            , payloadBlock(std::move(payloadBlock))
            , controlPool(ownerControlPool)
            , controlMemory(ownerControlMemory)
        {
        }

        void AddRef() noexcept
        {
            refCount.fetch_add(1, std::memory_order_relaxed);
        }

        void ReleaseRef() noexcept
        {
            if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                ReleaseLastRef();
            }
        }

        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept
        {
            return std::span<const std::byte>(payloadBlock.Data(), length);
        }

    private:
        void ReleaseLastRef() noexcept
        {
            NrMemoryPool* ownerPool = controlPool;
            std::byte* ownerMemory = controlMemory;

            // Do not release the control block storage before destroy_at(this).
            // After Release(ownerMemory), this memory belongs to the pool and this pointer must not be touched.
            // Copy ownerPool/ownerMemory to stack locals before destroy_at(this), then release the raw storage after object lifetime ends.
            std::destroy_at(this);
            static_cast<void>(ownerPool->Release(ownerMemory));
        }

    public:
        std::atomic_size_t refCount{1};
        std::size_t length = 0;

        NrPooledMemoryBlock payloadBlock;

        NrMemoryPool* controlPool = nullptr;
        std::byte* controlMemory = nullptr;
    };

    static_assert(sizeof(NrPayloadRefControlBlock) <= 64); // check for PayloadRef MemoryBlock size

    NrPayloadRef::NrPayloadRef(NrPayloadRef&& other) noexcept
        : control_(std::exchange(other.control_, nullptr))
    {
    }

    NrPayloadRef& NrPayloadRef::operator=(NrPayloadRef&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            control_ = std::exchange(other.control_, nullptr);
        }

        return *this;
    }

    NrPayloadRef::~NrPayloadRef() noexcept
    {
        Reset();
    }

    std::span<const std::byte> NrPayloadRef::Bytes() const noexcept
    {
        return control_ == nullptr ? std::span<const std::byte>() : control_->Bytes();
    }

    std::size_t NrPayloadRef::Length() const noexcept
    {
        return control_ == nullptr ? 0 : control_->length;
    }

    NrPayloadRef NrPayloadRef::Share() const noexcept
    {
        if (control_ == nullptr)
        {
            return NrPayloadRef();
        }

        control_->AddRef();
        return NrPayloadRef(control_);
    }

    NrPayloadRef::NrPayloadRef(NrPayloadRefControlBlock* control) noexcept
        : control_(control)
    {
    }

    void NrPayloadRef::Reset() noexcept
    {
        NrPayloadRefControlBlock* control = std::exchange(control_, nullptr);
        if (control != nullptr)
        {
            control->ReleaseRef();
        }
    }

    NrResult<NrPayloadRef> NrPayloadRefFactory::CreatePayloadRefFrom(NrMemoryPoolManager& memoryPoolManager,
                                                                     std::span<const std::byte> payloadBytes) noexcept
    {
        if (payloadBytes.empty())
        {
            return NrResult<NrPayloadRef>(NrPayloadRef());
        }

        NrMemoryPoolRole payloadRole = NrMemoryPoolRole::Count;
        if (!TrySelectPayloadRole(payloadBytes.size(), payloadRole))
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::CapacityExceeded);
        }

        // payload block
        NrResult<NrPooledMemoryBlock> payloadBlockResult = memoryPoolManager.AcquireBlock(payloadRole);
        if (payloadBlockResult.Failed())
        {
            return NrResult<NrPayloadRef>::Failure(payloadBlockResult.Status());
        }

        NrPooledMemoryBlock payloadBlock = payloadBlockResult.TakeValue();
        if (payloadBlock.Capacity() < payloadBytes.size())
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::CapacityExceeded);
        }

        std::copy(payloadBytes.begin(), payloadBytes.end(), payloadBlock.Data());

        return CreateFromOwnedBlock(memoryPoolManager, std::move(payloadBlock), payloadBytes.size());
    }

    NrResult<NrPayloadRef> NrPayloadRefFactory::CreateFramedPayloadRef(
        NrMemoryPoolManager& memoryPoolManager, const NrPacketType packetType,
        const std::span<const std::byte> semanticPayloadBytes) noexcept
    {
        if (semanticPayloadBytes.size() > NrMaxPacketLength - NrPacketHeaderLength)
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::CapacityExceeded);
        }

        const std::size_t framedLength = NrPacketHeaderLength + semanticPayloadBytes.size();
        NrMemoryPoolRole payloadRole = NrMemoryPoolRole::Count;
        if (!TrySelectPayloadRole(framedLength, payloadRole))
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::CapacityExceeded);
        }

        NrResult<NrPooledMemoryBlock> payloadBlockResult = memoryPoolManager.AcquireBlock(payloadRole);
        if (payloadBlockResult.Failed())
        {
            return NrResult<NrPayloadRef>::Failure(payloadBlockResult.Status());
        }

        NrPooledMemoryBlock payloadBlock = payloadBlockResult.TakeValue();
        if (payloadBlock.Capacity() < framedLength)
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::CapacityExceeded);
        }

        const NrPacketHeader header{
            static_cast<std::uint16_t>(framedLength),
            packetType.value,
            NrCurrentProtocolVersion,
            0,
        };
        NrPacketHeaderCodec::Encode(
            header, std::span<std::byte, NrPacketHeaderLength>(payloadBlock.Data(), NrPacketHeaderLength));
        std::copy(semanticPayloadBytes.begin(), semanticPayloadBytes.end(), payloadBlock.Data() + NrPacketHeaderLength);

        return CreateFromOwnedBlock(memoryPoolManager, std::move(payloadBlock), framedLength);
    }

    NrResult<NrPayloadRef> NrPayloadRefFactory::CreateFromOwnedBlock(NrMemoryPoolManager& memoryPoolManager,
                                                                     NrPooledMemoryBlock payloadBlock,
                                                                     const std::size_t payloadLength) noexcept
    {
        if (!payloadBlock.IsValid() || payloadLength == 0 || payloadLength > payloadBlock.Capacity())
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::InvalidArgument);
        }

        // control block
        NrMemoryPool* controlPool = memoryPoolManager.ResolvePool(NrMemoryPoolRole::PayloadRefControl);
        if (controlPool == nullptr)
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::InvalidState);
        }

        NrResult<std::byte*> controlMemoryResult = controlPool->Acquire();
        if (controlMemoryResult.Failed())
        {
            return NrResult<NrPayloadRef>::Failure(controlMemoryResult.Status());
        }

        std::byte* controlMemory = controlMemoryResult.TakeValue();
        if (controlMemory == nullptr)
        {
            return NrResult<NrPayloadRef>::Failure(NrErrorCode::InvalidState);
        }

        NrPayloadRefControlBlock* control =
            std::construct_at(reinterpret_cast<NrPayloadRefControlBlock*>(controlMemory), // placement-new
                              std::move(payloadBlock),                                    // payload memory block
                              payloadLength,                                              // payload length
                              controlPool,  // raw memory pool for control block
                              controlMemory // raw memory for control block
            );

        return NrResult<NrPayloadRef>(NrPayloadRef(control));
    }

    bool NrPayloadRefFactory::TrySelectPayloadRole(const std::size_t payloadLength, NrMemoryPoolRole& role) noexcept
    {
        if (payloadLength == 0)
        {
            role = NrMemoryPoolRole::Count;
            return false;
        }

        if (payloadLength <= 64)
        {
            role = NrMemoryPoolRole::Payload64;
            return true;
        }

        if (payloadLength <= 256)
        {
            role = NrMemoryPoolRole::Payload256;
            return true;
        }

        if (payloadLength <= 1024)
        {
            role = NrMemoryPoolRole::Payload1024;
            return true;
        }

        if (payloadLength <= NrMaxPacketLength)
        {
            role = NrMemoryPoolRole::Payload8192;
            return true;
        }

        return false;
    }

} // namespace psnr::core
