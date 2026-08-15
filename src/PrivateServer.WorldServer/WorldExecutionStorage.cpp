#include "pch.h"

#include "WorldExecutionStorage.h"

#include <new>
#include <utility>

namespace psnr::world
{
    WorldResult<std::unique_ptr<WorldExecutionStorage>> WorldExecutionStorage::Create(
        const WorldExecutionStorageConfig& config) noexcept
    {
        if (!IsValid(config.modes))
        {
            return WorldResult<std::unique_ptr<WorldExecutionStorage>>::Failure(WorldErrorCode::InvalidArgument);
        }

        std::unique_ptr<WorldIngressDoubleBuffer> inboundBuffer;
        if (config.modes.inboundMode == WorldInboundMode::DoubleBuffered)
        {
            WorldResult<std::unique_ptr<WorldIngressDoubleBuffer>> createResult =
                WorldIngressDoubleBuffer::Create(config.inboundEventCapacityPerSlot);
            if (createResult.Failed())
            {
                return WorldResult<std::unique_ptr<WorldExecutionStorage>>::Failure(createResult.Error());
            }
            inboundBuffer = createResult.TakeValue();
        }

        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer;
        if (config.modes.outboundMode == WorldOutboundMode::DoubleBuffered)
        {
            WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> createResult =
                WorldOutboundDoubleBuffer::Create(config.outboundCapacityPerSlot, config.outboundSlotCount);
            if (createResult.Failed())
            {
                return WorldResult<std::unique_ptr<WorldExecutionStorage>>::Failure(createResult.Error());
            }
            outboundBuffer = createResult.TakeValue();
        }

        WorldExecutionStorage* const storage =
            new (std::nothrow) WorldExecutionStorage(std::move(inboundBuffer), std::move(outboundBuffer));
        if (storage == nullptr)
        {
            return WorldResult<std::unique_ptr<WorldExecutionStorage>>::Failure(WorldErrorCode::AllocationFailed);
        }
        return WorldResult<std::unique_ptr<WorldExecutionStorage>>{std::unique_ptr<WorldExecutionStorage>{storage}};
    }

    WorldIngressDoubleBuffer* WorldExecutionStorage::InboundBuffer() noexcept
    {
        return inboundBuffer_.get();
    }

    WorldOutboundDoubleBuffer* WorldExecutionStorage::OutboundBuffer() noexcept
    {
        return outboundBuffer_.get();
    }

    WorldExecutionStorage::WorldExecutionStorage(std::unique_ptr<WorldIngressDoubleBuffer> inboundBuffer,
                                                 std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer) noexcept
        : inboundBuffer_(std::move(inboundBuffer))
        , outboundBuffer_(std::move(outboundBuffer))
    {
    }
} // namespace psnr::world
