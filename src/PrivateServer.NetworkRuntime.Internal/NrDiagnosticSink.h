#pragma once

#include "NrDiagnosticRecord.h"
#include "NrDiagnosticsConfig.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    struct NrDiagnosticRunMetadata final
    {
        NrDiagnosticsMode mode = NrDiagnosticsMode::Disabled;
    };

    struct NrDiagnosticSummary final
    {
        std::uint64_t attempted = 0;
        std::uint64_t enqueued = 0;
        std::uint64_t droppedQueueFull = 0;
        std::uint64_t droppedSinkUnavailable = 0;
        std::uint64_t consumed = 0;
        std::uint64_t discardedAfterSinkFailure = 0;
    };

    class INrDiagnosticSink // 공용 sink 계약
    {
    public:
        INrDiagnosticSink() noexcept = default;

        INrDiagnosticSink(const INrDiagnosticSink&) = delete;
        INrDiagnosticSink& operator=(const INrDiagnosticSink&) = delete;

        virtual ~INrDiagnosticSink() noexcept = default;

        [[nodiscard]] virtual psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata& metadata) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus Consume(const NrDiagnosticRecord& record) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus Finish(const NrDiagnosticSummary& summary) noexcept = 0;
    };
} // namespace psnr::runtime::internal
