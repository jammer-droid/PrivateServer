#pragma once

#include "ApplicationLogConfig.h"
#include "ApplicationLogFanoutSink.h"

#include <memory>

namespace psnr::logging::internal
{
    class ApplicationLogOutputFactory final
    {
    public:
        [[nodiscard]] static std::unique_ptr<IApplicationLogOutput> CreateRotatingFile(
            const ApplicationLogConfig& config);
        [[nodiscard]] static std::unique_ptr<IApplicationLogOutput> CreateConsole();
    };
} // namespace psnr::logging::internal
