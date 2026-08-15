#include "pch.h"

#include "NrClientMemoryPoolConfigFactory.h"

#include "NrBoundedMpscQueue.h"
#include "NrMemoryMath.h"
#include "NrPacketHeader.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrMemoryPoolConfig;
    using psnr::core::NrMemoryPoolManagerConfig;
    using psnr::core::NrMemoryPoolManagerPoolConfig;
    using psnr::core::NrMemoryPoolRole;
    using psnr::core::NrResult;

    namespace
    {
        constexpr std::size_t ClientIoContextBlockCount = 3;
        constexpr std::size_t PayloadRefControlBlockSize = 64;

        [[nodiscard]] constexpr NrMemoryPoolConfig MakePoolConfig(
            std::size_t blockSize, std::size_t blockCount, std::size_t alignment = psnr::core::NrCacheLineSize) noexcept
        {
            return NrMemoryPoolConfig{blockSize, blockCount, alignment};
        }

        [[nodiscard]] bool CanAllocatePool(const NrMemoryPoolConfig& pool) noexcept
        {
            std::size_t unusedStorageBytes = 0;
            return psnr::core::utils::NrTryMultiply(pool.blockSize, pool.blockCount, unusedStorageBytes);
        }

        void SetPool(NrMemoryPoolManagerConfig& config, NrMemoryPoolRole role, const NrMemoryPoolConfig& pool) noexcept
        {
            const std::size_t roleIndex = static_cast<std::size_t>(role);
            config.pools[roleIndex] = NrMemoryPoolManagerPoolConfig{role, pool};
        }
    } // namespace

    NrResult<NrMemoryPoolManagerConfig> NrClientMemoryPoolConfigFactory::Create(
        const NrClientMemoryPoolSizing& sizing) noexcept
    {
        if (sizing.eventQueueStorageBytes == 0 || sizing.payloadQueueStorageBytes == 0 ||
            sizing.payloadQueueCapacity == 0)
        {
            return NrResult<NrMemoryPoolManagerConfig>::Failure(NrErrorCode::InvalidArgument);
        }

        const std::size_t sendPayloadBlockCount = sizing.payloadQueueCapacity;
        const std::size_t overlappedContextBytes = std::max(sizeof(NrRecvIoContext), sizeof(NrSendIoContext));
        const std::array<NrMemoryPoolConfig, 9> pools = {
            MakePoolConfig(psnr::core::NrMaxPacketLength, 1),
            MakePoolConfig(overlappedContextBytes, ClientIoContextBlockCount),
            MakePoolConfig(sizing.payloadQueueStorageBytes, 1),
            MakePoolConfig(sizing.eventQueueStorageBytes, 1),
            MakePoolConfig(64, sendPayloadBlockCount),
            MakePoolConfig(256, sendPayloadBlockCount),
            MakePoolConfig(1024, sendPayloadBlockCount),
            MakePoolConfig(8192, sendPayloadBlockCount),
            MakePoolConfig(PayloadRefControlBlockSize, sendPayloadBlockCount),
        };

        for (const NrMemoryPoolConfig& pool : pools)
        {
            if (!CanAllocatePool(pool))
            {
                return NrResult<NrMemoryPoolManagerConfig>::Failure(NrErrorCode::CapacityExceeded);
            }
        }

        NrMemoryPoolManagerConfig config;
        SetPool(config, NrMemoryPoolRole::RecvBuffer, pools[0]);
        SetPool(config, NrMemoryPoolRole::OverlappedContext, pools[1]);
        SetPool(config, NrMemoryPoolRole::ClientPayloadQueueStorage, pools[2]);
        SetPool(config, NrMemoryPoolRole::ClientEventQueueStorage, pools[3]);
        SetPool(config, NrMemoryPoolRole::Payload64, pools[4]);
        SetPool(config, NrMemoryPoolRole::Payload256, pools[5]);
        SetPool(config, NrMemoryPoolRole::Payload1024, pools[6]);
        SetPool(config, NrMemoryPoolRole::Payload8192, pools[7]);
        SetPool(config, NrMemoryPoolRole::PayloadRefControl, pools[8]);
        return NrResult<NrMemoryPoolManagerConfig>(config);
    }
} // namespace psnr::runtime::internal
