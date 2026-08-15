#pragma once

#include "ApplicationLogRecord.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace psnr::logging::internal
{
    inline constexpr std::size_t MaximumApplicationLogMessageBytes = 1'024;

    // ApplicationLogRecord 를 async backend 로 넘기기 위한 envelope
    struct ApplicationLogEnvelope final
    {
        // On failure, output is not modified. Allocation failures propagate to the noexcept logger boundary.
        [[nodiscard]] static bool TryCreate(const ApplicationLogRecord& record,
                                            std::chrono::system_clock::time_point producerTimestampUtc,
                                            std::uint64_t producerSteadyNs, ApplicationLogEnvelope* output);

        std::chrono::system_clock::time_point producerTimestampUtc{};
        std::uint64_t producerSteadyNs = 0;
        ApplicationLogRecord record;
        bool messageTruncated = false;
    };
} // namespace psnr::logging::internal
