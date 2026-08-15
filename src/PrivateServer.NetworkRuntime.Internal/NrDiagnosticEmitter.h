#pragma once

#include "NrDiagnosticRecord.h"

#include <atomic>
#include <cstdint>

namespace psnr::runtime::internal
{
    class NrDiagnosticsComponent;
    class NrDiagnosticEmitterState;

    class NrDiagnosticEmitter final
    {
    public:
        NrDiagnosticEmitter() noexcept = default;

        NrDiagnosticEmitter(const NrDiagnosticEmitter&) noexcept = default;
        NrDiagnosticEmitter& operator=(const NrDiagnosticEmitter&) noexcept = default;

        NrDiagnosticEmitter(NrDiagnosticEmitter&&) noexcept = default;
        NrDiagnosticEmitter& operator=(NrDiagnosticEmitter&&) noexcept = default;

        ~NrDiagnosticEmitter() noexcept = default;

        [[nodiscard]] bool IsEnabled() const noexcept;
        void Emit(NrDiagnosticRecord record) const noexcept;

    private:
        friend class NrDiagnosticsComponent;

        explicit NrDiagnosticEmitter(NrDiagnosticEmitterState& state) noexcept;

        static void IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept;
        [[nodiscard]] static std::uint64_t ProducerTimestamp() noexcept;
        [[nodiscard]] static bool TryEnter(NrDiagnosticEmitterState& state) noexcept;
        static void Leave(NrDiagnosticEmitterState& state) noexcept;
        static void WakeConsumer(NrDiagnosticEmitterState& state) noexcept;

        // Non-owning. The diagnostics component must outlive every use of this handle.
        NrDiagnosticEmitterState* state_ = nullptr;
    };
} // namespace psnr::runtime::internal
