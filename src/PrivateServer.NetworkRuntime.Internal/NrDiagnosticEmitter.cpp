#include "pch.h"

#include "NrDiagnosticEmitter.h"

#include "NrDiagnosticEmitterState.h"

#include <chrono>
#include <limits>

namespace psnr::runtime::internal
{
    // !TODO: 공용 Saturating 함수에서 기능 제공하기
    void NrDiagnosticEmitter::IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept
    {
        constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();

        std::uint64_t observed = counter.load(std::memory_order_relaxed);
        while (observed != MaxValue && !counter.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed,
                                                                      std::memory_order_relaxed))
        {
        }
    }

    // !TODO: 공용 Timer 에서 기능 제공하기
    std::uint64_t NrDiagnosticEmitter::ProducerTimestamp() noexcept
    {
        const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

    bool NrDiagnosticEmitter::TryEnter(NrDiagnosticEmitterState& state) noexcept
    {
        std::uint64_t admission = state.admissionState_.load(std::memory_order_acquire);
        while ((admission & NrDiagnosticEmitterState::EmitterAdmissionClosed) == 0) // check admission
        {
            if ((admission & NrDiagnosticEmitterState::EmitterCountMask) == NrDiagnosticEmitterState::EmitterCountMask)
            {
                // full of in-flight producers
                return false;
            }

            if (state.admissionState_.compare_exchange_weak(admission, admission + 1, std::memory_order_acquire,
                                                            std::memory_order_acquire))
            {
                return true;
            }
        }

        return false;
    }

    void NrDiagnosticEmitter::Leave(NrDiagnosticEmitterState& state) noexcept
    {
        const std::uint64_t previousState = state.admissionState_.fetch_sub(1, std::memory_order_release);
        if ((previousState & NrDiagnosticEmitterState::EmitterCountMask) == 1) // check last
        {
            state.admissionState_.notify_one();
        }
    }

    void NrDiagnosticEmitter::WakeConsumer(NrDiagnosticEmitterState& state) noexcept
    {
        state.wakeVersion_.fetch_add(1, std::memory_order_release);
        state.wakeVersion_.notify_one();
    }

    NrDiagnosticEmitter::NrDiagnosticEmitter(NrDiagnosticEmitterState& state) noexcept
        : state_(&state)
    {
    }

    bool NrDiagnosticEmitter::IsEnabled() const noexcept
    {
        if (state_ == nullptr || state_->shutdown_.load(std::memory_order_acquire))
        {
            return false;
        }

        const std::uint64_t admission = state_->admissionState_.load(std::memory_order_acquire);
        return (admission & NrDiagnosticEmitterState::EmitterAdmissionClosed) == 0;
    }

    void NrDiagnosticEmitter::Emit(NrDiagnosticRecord record) const noexcept
    {
        NrDiagnosticEmitterState* state = state_;
        if (state == nullptr || state->shutdown_.load(std::memory_order_acquire))
        {
            return;
        }

        if (!TryEnter(*state))
        {
            // shutdown == true 인 경우, TryEnter 실패는 오류, diagnostics 손실로 간주하지 않음.
            //      - 의도된 lifecycle 종료이며, admission 을 닫은 상태
            // shutdown == false 인 경우, sink failure 로 인한 TryEnter 실패인지 확인
            if (!state->shutdown_.load(std::memory_order_acquire) && state->sinkFailed_.load(std::memory_order_relaxed))
            {
                IncrementSaturating(state->attempted_);
                IncrementSaturating(state->droppedSinkUnavailable_);
            }
            return;
        }

        IncrementSaturating(state->attempted_);
        if (state->sinkFailed_.load(std::memory_order_acquire))
        {
            IncrementSaturating(state->droppedSinkUnavailable_);
            Leave(*state);
            return;
        }

        record.producerTimestamp = ProducerTimestamp();
        record.drainSequence = 0;

        const psnr::core::NrStatus pushStatus = state->queue_->TryPush(std::move(record));
        if (pushStatus.Failed())
        {
            IncrementSaturating(state->droppedQueueFull_);
            Leave(*state);
            return;
        }

        IncrementSaturating(state->enqueued_);
        WakeConsumer(*state);
        Leave(*state);
    }
} // namespace psnr::runtime::internal
