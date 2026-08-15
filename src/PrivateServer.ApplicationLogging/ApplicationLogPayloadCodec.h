#pragma once

#include "ApplicationLogEnvelope.h"

#include <string>
#include <string_view>

namespace psnr::logging::internal
{
    class ApplicationLogPayloadCodec final
    {
    public:
        [[nodiscard]] static std::string Encode(const ApplicationLogEnvelope& envelope);

        // On failure, output is not modified.
        [[nodiscard]] static bool TryDecode(std::string_view payload, ApplicationLogEnvelope* output) noexcept;
    };
} // namespace psnr::logging::internal
