#pragma once

#include <cstddef>

namespace psnr::runtime
{
    inline constexpr std::size_t NrDefaultClientEventQueueCapacity = 128;
    inline constexpr std::size_t NrDefaultClientPayloadQueueCapacity = 1024;

    struct NrClientConfig final
    {
        std::size_t eventQueueCapacity = NrDefaultClientEventQueueCapacity;
        std::size_t payloadQueueCapacity = NrDefaultClientPayloadQueueCapacity;
    };
} // namespace psnr::runtime
