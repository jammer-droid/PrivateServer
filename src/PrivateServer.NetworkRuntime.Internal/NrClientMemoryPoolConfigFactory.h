#pragma once

#include "NrMemoryPoolManager.h"
#include "NrResult.h"

#include <cstddef>

namespace psnr::runtime::internal
{
    struct NrClientMemoryPoolSizing final
    {
        std::size_t eventQueueStorageBytes = 0;
        std::size_t payloadQueueStorageBytes = 0;
        std::size_t payloadQueueCapacity = 0;
    };

    class NrClientMemoryPoolConfigFactory final
    {
    public:
        [[nodiscard]] static psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig> Create(
            const NrClientMemoryPoolSizing& sizing) noexcept;
    };
} // namespace psnr::runtime::internal
