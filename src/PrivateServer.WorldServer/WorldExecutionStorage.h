#pragma once

#include "WorldExecutionModeConfig.h"
#include "WorldIngressDoubleBuffer.h"
#include "WorldOutboundDoubleBuffer.h"
#include "WorldResult.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace psnr::world
{
    struct WorldExecutionStorageConfig final
    {
        WorldExecutionModeConfig modes{};
        std::size_t inboundEventCapacityPerSlot = 0;
        WorldOutboundBatchCapacity outboundCapacityPerSlot{};
        WorldOutboundBufferSlotCount outboundSlotCount = WorldOutboundBufferSlotCount::Double;
    };

    // Runtime와 worker를 시작하기 전에 선택된 mode에 필요한 storage만 준비한다.
    class WorldExecutionStorage final
    {
    public:
        WorldExecutionStorage(const WorldExecutionStorage&) = delete;
        WorldExecutionStorage& operator=(const WorldExecutionStorage&) = delete;

        [[nodiscard]] static WorldResult<std::unique_ptr<WorldExecutionStorage>> Create(
            const WorldExecutionStorageConfig& config) noexcept;

        [[nodiscard]] WorldIngressDoubleBuffer* InboundBuffer() noexcept;
        [[nodiscard]] WorldOutboundDoubleBuffer* OutboundBuffer() noexcept;

    private:
        WorldExecutionStorage(std::unique_ptr<WorldIngressDoubleBuffer> inboundBuffer,
                              std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer) noexcept;

        std::unique_ptr<WorldIngressDoubleBuffer> inboundBuffer_;
        std::unique_ptr<WorldOutboundDoubleBuffer> outboundBuffer_;
    };
} // namespace psnr::world
