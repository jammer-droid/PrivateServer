#include "pch.h"

#include "NrIocpOverlappedContextFactory.h"

#include "NrMemoryPoolManager.h"
#include "NrPayloadRef.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrMemoryPoolRole;
    using psnr::core::NrPooledMemoryBlock;
    using psnr::core::NrResult;
    using psnr::core::NrSessionKey;

    namespace
    {
        template <typename T> [[nodiscard]] bool CanStoreIoContext(const NrPooledMemoryBlock& contextBlock) noexcept
        {
            const uintptr_t blockAddress = reinterpret_cast<std::uintptr_t>(contextBlock.Data());

            return contextBlock.Capacity() >= sizeof(T) && contextBlock.Stride() >= sizeof(T) &&
                   (blockAddress % alignof(T)) == 0;
        }

        template <typename T>
        [[nodiscard]] NrResult<NrPooledMemoryBlock> AcquireContextBlock(NrMemoryPoolManager& memoryPoolManager) noexcept
        {
            NrResult<NrPooledMemoryBlock> contextStorageResult =
                memoryPoolManager.AcquireBlock(NrMemoryPoolRole::OverlappedContext);
            if (contextStorageResult.Failed())
            {
                return NrResult<NrPooledMemoryBlock>::Failure(contextStorageResult.Status());
            }

            NrPooledMemoryBlock contextBlock = contextStorageResult.TakeValue();
            if (!CanStoreIoContext<T>(contextBlock))
            {
                return NrResult<NrPooledMemoryBlock>::Failure(NrErrorCode::CapacityExceeded);
            }

            return NrResult<NrPooledMemoryBlock>(std::move(contextBlock));
        }

        [[nodiscard]] bool CanUseWsaBufferLength(std::size_t length) noexcept
        {
            return length > 0 && length <= std::numeric_limits<ULONG>::max();
        }

    } // namespace

    NrIocpOverlappedContextFactory::NrIocpOverlappedContextFactory(NrMemoryPoolManager& memoryPoolManager) noexcept
        : memoryPoolManager_(memoryPoolManager)
    {
    }

    NrResult<NrRecvIoContextLease> NrIocpOverlappedContextFactory::CreateRecv(
        NrSessionKey sessionKey, std::span<std::byte> writableBuffer) noexcept
    {
        if (sessionKey == 0 || writableBuffer.data() == nullptr || !CanUseWsaBufferLength(writableBuffer.size()))
        {
            return NrResult<NrRecvIoContextLease>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<NrPooledMemoryBlock> contextBlockResult = AcquireContextBlock<NrRecvIoContext>(memoryPoolManager_);
        if (contextBlockResult.Failed())
        {
            return NrResult<NrRecvIoContextLease>::Failure(contextBlockResult.Status());
        }

        NrPooledMemoryBlock contextBlock = contextBlockResult.TakeValue();
        std::construct_at(reinterpret_cast<NrRecvIoContext*>(contextBlock.Data()), sessionKey, writableBuffer);

        return NrResult<NrRecvIoContextLease>(NrRecvIoContextLease(std::move(contextBlock)));
    }

    NrResult<NrSendIoContextLease> NrIocpOverlappedContextFactory::CreateSendContext(
        const NrSessionKey sessionKey) noexcept
    {
        if (sessionKey == 0)
        {
            return NrResult<NrSendIoContextLease>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<NrPooledMemoryBlock> contextBlockResult = AcquireContextBlock<NrSendIoContext>(memoryPoolManager_);
        if (contextBlockResult.Failed())
        {
            return NrResult<NrSendIoContextLease>::Failure(contextBlockResult.Status());
        }

        NrPooledMemoryBlock contextBlock = contextBlockResult.TakeValue();
        std::construct_at(reinterpret_cast<NrSendIoContext*>(contextBlock.Data()), sessionKey, NrPayloadRef{});

        return NrResult<NrSendIoContextLease>(NrSendIoContextLease(std::move(contextBlock)));
    }

    NrResult<NrSendIoContextLease> NrIocpOverlappedContextFactory::CreateSend(
        NrSessionKey sessionKey, std::span<const std::byte> payload) noexcept
    {
        if (sessionKey == 0 || payload.data() == nullptr || !CanUseWsaBufferLength(payload.size()))
        {
            return NrResult<NrSendIoContextLease>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<psnr::core::NrPayloadRef> payloadRefResult =
            psnr::core::NrPayloadRefFactory::CreatePayloadRefFrom(memoryPoolManager_, payload);
        if (payloadRefResult.Failed())
        {
            return NrResult<NrSendIoContextLease>::Failure(payloadRefResult.Status());
        }

        return CreateSend(sessionKey, payloadRefResult.TakeValue());
    }

    NrResult<NrSendIoContextLease> NrIocpOverlappedContextFactory::CreateSend(NrSessionKey sessionKey,
                                                                              psnr::core::NrPayloadRef payload) noexcept
    {
        if (sessionKey == 0 || payload.IsEmpty() || !CanUseWsaBufferLength(payload.Length()))
        {
            return NrResult<NrSendIoContextLease>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<NrPooledMemoryBlock> contextBlockResult = AcquireContextBlock<NrSendIoContext>(memoryPoolManager_);
        if (contextBlockResult.Failed())
        {
            return NrResult<NrSendIoContextLease>::Failure(contextBlockResult.Status());
        }

        NrPooledMemoryBlock contextBlock = contextBlockResult.TakeValue();
        std::construct_at(reinterpret_cast<NrSendIoContext*>(contextBlock.Data()), sessionKey, std::move(payload));

        return NrResult<NrSendIoContextLease>(NrSendIoContextLease(std::move(contextBlock)));
    }
} // namespace psnr::runtime
