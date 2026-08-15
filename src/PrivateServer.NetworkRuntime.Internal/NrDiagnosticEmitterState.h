#pragma once

#include "NrBoundedMpscQueue.h"
#include "NrDiagnosticRecord.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace psnr::runtime::internal
{
    class NrDiagnosticEmitter;
    class NrDiagnosticsComponent;

    class NrDiagnosticEmitterState final
    {
    public:
        using NrQueue = psnr::core::NrBoundedMpscQueue<NrDiagnosticRecord>;

        explicit NrDiagnosticEmitterState(std::unique_ptr<NrQueue> queue) noexcept
            : queue_(std::move(queue))
        {
        }

        NrDiagnosticEmitterState(const NrDiagnosticEmitterState&) = delete;
        NrDiagnosticEmitterState& operator=(const NrDiagnosticEmitterState&) = delete;

        NrDiagnosticEmitterState(NrDiagnosticEmitterState&&) = delete;
        NrDiagnosticEmitterState& operator=(NrDiagnosticEmitterState&&) = delete;

        ~NrDiagnosticEmitterState() noexcept = default;

    private:
        friend class NrDiagnosticEmitter;
        friend class NrDiagnosticsComponent;

        // High bit : 신규 emit admission 차단 여부. 1 - 차단, 0 - 허용
        // Low bits : 현재 진입한 in-flight emitter 수( = TryEnter ~ Leave 구간에 있는 producer의 수)
        static constexpr std::uint64_t EmitterAdmissionClosed = std::uint64_t{1} << 63;
        static constexpr std::uint64_t EmitterCountMask = EmitterAdmissionClosed - 1;

        std::unique_ptr<NrQueue> queue_;

        std::atomic<std::uint64_t> wakeVersion_{0};
        std::atomic<std::uint64_t> admissionState_{EmitterAdmissionClosed};
        std::atomic_bool shutdown_{false};
        std::atomic_bool sinkFailed_{false}; // only changed in consumer thread

        std::atomic<std::uint64_t> attempted_{0};
        std::atomic<std::uint64_t> enqueued_{0};
        std::atomic<std::uint64_t> droppedQueueFull_{0};
        std::atomic<std::uint64_t> droppedSinkUnavailable_{0};
        std::atomic<std::uint64_t> consumed_{0};
        std::atomic<std::uint64_t> discardedAfterSinkFailure_{0};
    };
} // namespace psnr::runtime::internal
