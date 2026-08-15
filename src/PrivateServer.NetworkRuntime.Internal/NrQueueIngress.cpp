#include "pch.h"

#include "NrQueueIngress.h"

#include "NrBoundedMpscQueue.h"

#include <utility>

namespace psnr::core
{
    NrQueueIngress::NrQueueIngress(NrBoundedMpscQueue<NrInput>& queue) noexcept
        : queue_(queue)
    {
    }

    NrStatus NrQueueIngress::TryEnqueue(NrInput&& input) noexcept
    {
        return queue_.TryPush(std::move(input));
    }
} // namespace psnr::core
