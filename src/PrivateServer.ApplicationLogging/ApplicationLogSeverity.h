#pragma once

#include <cstdint>

namespace psnr::logging
{
    enum class ApplicationLogSeverity : std::uint8_t
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Critical = 4,
    };
} // namespace psnr::logging
