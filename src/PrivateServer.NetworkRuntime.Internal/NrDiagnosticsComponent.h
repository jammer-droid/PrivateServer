#pragma once

#include "NrDiagnosticEmitter.h"
#include "NrDiagnosticEmitterState.h"
#include "NrDiagnosticsConfigInternal.h"
#include "NrDiagnosticSink.h"
#include "NrLifecycleInternal.h"
#include "NrMemoryPoolManager.h"
#include "NrResult.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace psnr::runtime::internal
{
    struct NrDiagnosticsStats final
    {
        bool enabled = false;
        bool sinkFailed = false;
        std::uint64_t attempted = 0;
        std::uint64_t enqueued = 0;
        std::uint64_t droppedQueueFull = 0;
        std::uint64_t droppedSinkUnavailable = 0;
        std::uint64_t consumed = 0;
        std::uint64_t discardedAfterSinkFailure = 0;
    };

    class NrDiagnosticsComponent final : public INrServerLifecycleComponent
    {
    public:
        NrDiagnosticsComponent(const NrDiagnosticsComponent&) = delete;
        NrDiagnosticsComponent& operator=(const NrDiagnosticsComponent&) = delete;

        NrDiagnosticsComponent(NrDiagnosticsComponent&&) = delete;
        NrDiagnosticsComponent& operator=(NrDiagnosticsComponent&&) = delete;

        ~NrDiagnosticsComponent() noexcept override;

        [[nodiscard]] static psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> Create(
            const NrDiagnosticsConfigInternal& config, psnr::core::NrMemoryPoolManager& memoryPoolManager,
            std::unique_ptr<INrDiagnosticSink> sink) noexcept;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override;
        [[nodiscard]] NrStatus Start() noexcept override;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override;
        [[nodiscard]] NrStatus Shutdown() noexcept override;

        [[nodiscard]] NrDiagnosticEmitter Emitter() noexcept;
        [[nodiscard]] NrDiagnosticsStats CaptureStats() const noexcept;

    private:
        enum class NrSinkStartState : std::uint8_t
        {
            NotStarted, // consumer thread 생성 전 or 생성 실패
            Pending,    // consumer thread 가 sink->Begin() 수행 중
            Succeeded,  // sink->Begin() 성공
            Failed,     // sink->Begin() 실패
        };

        NrDiagnosticsComponent(NrDiagnosticsMode mode, std::unique_ptr<NrDiagnosticEmitterState::NrQueue> queue,
                               std::unique_ptr<INrDiagnosticSink> sink) noexcept;

        static void IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept;

        void CloseEmitterAdmission() noexcept;
        void WaitForEmitterQuiescence() noexcept;
        void WakeConsumer() noexcept;
        void ConsumerLoop() noexcept;
        void DrainAvailable() noexcept;
        [[nodiscard]] NrDiagnosticSummary BuildSummary() const noexcept;

        const NrDiagnosticsMode mode_ = NrDiagnosticsMode::Disabled;
        const bool enabled_ = false;
        NrDiagnosticEmitterState emitterState_;
        std::unique_ptr<INrDiagnosticSink> sink_;

        std::thread consumerThread_;
        std::atomic<NrSinkStartState> sinkStartState_{NrSinkStartState::NotStarted};
        std::atomic_bool shutdownRequested_{false};

        std::uint64_t nextDrainSequence_ = 1;
        bool configured_ = false;
        bool started_ = false;
        bool shutdown_ = false;
    };
} // namespace psnr::runtime::internal
