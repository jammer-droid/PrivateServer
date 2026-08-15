#pragma once

#include "ApplicationLogConfig.h"
#include "ApplicationLogEnvelope.h"

#include <cstdint>
#include <string>

namespace psnr::logging::internal
{
    // Non-owning view used only for the duration of a synchronous formatting call.
    struct ApplicationLogFormatInput final
    {
        const ApplicationLogConfig& config;
        const ApplicationLogEnvelope& envelope;
        std::uint64_t drainSequence = 0;
    };

    class ApplicationLogFormatter final
    {
    public:
        // Returned payloads do not contain a terminating newline. The sink owns line termination.
        [[nodiscard]] static std::string FormatJsonPayload(const ApplicationLogFormatInput& input);
        [[nodiscard]] static std::string FormatConsolePayload(const ApplicationLogFormatInput& input);
    };
} // namespace psnr::logging::internal
