#pragma once

#include <cstdint>

namespace psnr::logging
{
    // 로거가 지금까지 정상적으로 동작했는지
    struct ApplicationLogHealth final
    {
        bool started = false;
        bool fileSinkFailed = false;
        bool consoleSinkFailed = false;
        std::uint64_t attempted = 0;
        std::uint64_t filtered = 0;
        std::uint64_t enqueued = 0;
        std::uint64_t consumed = 0;
        std::uint64_t droppedQueueFull = 0;
        std::uint64_t discardedAfterSinkFailure = 0;
        std::uint64_t currentQueueDepth = 0;
        std::uint64_t maximumQueueDepth = 0;
    };
} // namespace psnr::logging
