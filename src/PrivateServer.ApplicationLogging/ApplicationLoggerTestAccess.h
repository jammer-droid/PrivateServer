#pragma once

#include "ApplicationLogConfig.h"
#include "ApplicationLogFanoutSink.h"

#include <memory>

namespace psnr::logging
{
    class ApplicationLogger;
}

namespace psnr::logging::internal
{
    class ApplicationLoggerTestAccess final
    {
    public:
        [[nodiscard]] static std::unique_ptr<ApplicationLogger> Create(
            const ApplicationLogConfig& config, std::unique_ptr<IApplicationLogOutput> fileOutput,
            std::unique_ptr<IApplicationLogOutput> consoleOutput) noexcept;
    };
} // namespace psnr::logging::internal
