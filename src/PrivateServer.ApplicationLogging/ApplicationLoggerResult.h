#pragma once

#include <cstdint>

namespace psnr::logging
{
    enum class ApplicationLoggerResult : std::uint8_t
    {
        Success,
        InvalidArgument,
        InvalidConfig,
        OutputUnavailable,
        InvalidState,
        ResourceUnavailable,
    };
} // namespace psnr::logging
