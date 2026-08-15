#pragma once

#include "NrDiagnosticsConfig.h"
#include "NrResult.h"

#include <cstddef>
#include <string>

namespace psnr::runtime::internal
{
    struct NrDiagnosticsConfigInternal final
    {
        static constexpr std::size_t QueueCapacity = 1024;

        NrDiagnosticsMode mode = NrDiagnosticsMode::Disabled;
        std::string outputPath;
    };

    [[nodiscard]] psnr::core::NrResult<NrDiagnosticsConfigInternal> CreateDiagnosticsConfigInternal(
        const NrDiagnosticsConfig& config) noexcept;
} // namespace psnr::runtime::internal
