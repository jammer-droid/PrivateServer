#pragma once

#include "NrDiagnosticsConfigInternal.h"
#include "NrDiagnosticSink.h"
#include "NrStatus.h"

#include <memory>

namespace psnr::runtime
{
    using psnr::core::NrStatus;

    struct NrServerConfig;
    class NrServerComponentGraph;

    [[nodiscard]] NrStatus BuildServerGraph(
        const NrServerConfig& config, const internal::NrDiagnosticsConfigInternal& diagnosticsConfig,
        NrServerComponentGraph& graph, std::unique_ptr<internal::INrDiagnosticSink> diagnosticsSink = nullptr) noexcept;

} // namespace psnr::runtime
