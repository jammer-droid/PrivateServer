#pragma once

#include "NrDiagnosticSink.h"
#include "NrResult.h"

#include <memory>
#include <string_view>

namespace psnr::runtime::internal
{
    [[nodiscard]] psnr::core::NrResult<std::unique_ptr<INrDiagnosticSink>> CreateJsonlDiagnosticSink(
        std::string_view outputPathUtf8) noexcept;
} // namespace psnr::runtime::internal
