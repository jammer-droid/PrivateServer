#pragma once

#include "ApplicationLogContext.h"
#include "ApplicationLogSeverity.h"

#include <optional>
#include <string>

namespace psnr::logging
{
    struct ApplicationLogRecord final
    {
        [[nodiscard]] static bool IsValid(const ApplicationLogRecord& record) noexcept;

        ApplicationLogSeverity severity = ApplicationLogSeverity::Info;
        std::string component;
        std::string event;
        std::optional<std::string> message;
        ApplicationLogContext context;
    };
} // namespace psnr::logging
