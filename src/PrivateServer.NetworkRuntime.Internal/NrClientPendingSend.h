#pragma once

#include "NrBoundedMpscQueue.h"
#include "NrPayloadRef.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    struct NrClientPendingSend final
    {
        std::uint64_t attemptGeneration = 0;
        psnr::core::NrPayloadRef payload;
    };

    using NrClientPendingSendQueue = psnr::core::NrBoundedMpscQueue<NrClientPendingSend>;
} // namespace psnr::runtime::internal
