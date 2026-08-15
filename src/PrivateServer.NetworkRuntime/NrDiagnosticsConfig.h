#pragma once

#include "NrUtf8View.h"

#include <cstdint>

namespace psnr::runtime
{
    enum class NrDiagnosticsMode : std::uint8_t
    {
        Disabled,
        Debug,
        Benchmark,
    };

    struct NrDiagnosticsConfig final
    {
        NrDiagnosticsMode mode = NrDiagnosticsMode::Disabled;
        NrUtf8View outputPath{};
    };
} // namespace psnr::runtime
