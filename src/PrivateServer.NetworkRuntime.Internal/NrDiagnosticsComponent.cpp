#include "pch.h"

#include "NrDiagnosticsComponent.h"

#include "NrErrorCode.h"

#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrResult;

    NrDiagnosticsComponent::NrDiagnosticsComponent(NrDiagnosticsMode mode,
                                                   std::unique_ptr<NrDiagnosticEmitterState::NrQueue> queue,
                                                   std::unique_ptr<INrDiagnosticSink> sink) noexcept
        : mode_(mode)
        , enabled_(mode != NrDiagnosticsMode::Disabled)
        , emitterState_(std::move(queue))
        , sink_(std::move(sink))
    {
    }

    NrDiagnosticsComponent::~NrDiagnosticsComponent() noexcept
    {
        static_cast<void>(Shutdown());
    }

    NrResult<std::unique_ptr<NrDiagnosticsComponent>> NrDiagnosticsComponent::Create(
        const NrDiagnosticsConfigInternal& config, psnr::core::NrMemoryPoolManager& memoryPoolManager,
        std::unique_ptr<INrDiagnosticSink> sink) noexcept
    {
        if (config.mode == NrDiagnosticsMode::Disabled)
        {
            if (sink != nullptr)
            {
                return NrResult<std::unique_ptr<NrDiagnosticsComponent>>::Failure(NrErrorCode::InvalidArgument);
            }

            std::unique_ptr<NrDiagnosticsComponent> component(
                new (std::nothrow) NrDiagnosticsComponent(config.mode, nullptr, nullptr));
            if (component == nullptr)
            {
                return NrResult<std::unique_ptr<NrDiagnosticsComponent>>::Failure(NrErrorCode::OutOfMemory);
            }

            return NrResult<std::unique_ptr<NrDiagnosticsComponent>>(std::move(component));
        }

        if ((config.mode != NrDiagnosticsMode::Debug && config.mode != NrDiagnosticsMode::Benchmark) || sink == nullptr)
        {
            return NrResult<std::unique_ptr<NrDiagnosticsComponent>>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<std::unique_ptr<NrDiagnosticEmitterState::NrQueue>> queueResult =
            NrDiagnosticEmitterState::NrQueue::Create(memoryPoolManager,
                                                      psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                                                      NrDiagnosticsConfigInternal::QueueCapacity);
        if (queueResult.Failed())
        {
            return NrResult<std::unique_ptr<NrDiagnosticsComponent>>::Failure(queueResult.Status());
        }

        std::unique_ptr<NrDiagnosticsComponent> component(
            new (std::nothrow) NrDiagnosticsComponent(config.mode, queueResult.TakeValue(), std::move(sink)));
        if (component == nullptr)
        {
            return NrResult<std::unique_ptr<NrDiagnosticsComponent>>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<std::unique_ptr<NrDiagnosticsComponent>>(std::move(component));
    }

    NrStatus NrDiagnosticsComponent::Configure(NrBootstrapContext& context) noexcept
    {
        static_cast<void>(context);
        if (configured_ || shutdown_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        configured_ = true;
        return NrStatus::Success();
    }

    NrStatus NrDiagnosticsComponent::Start() noexcept
    {
        if (!configured_ || started_ || shutdown_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (!enabled_)
        {
            started_ = true;
            return NrStatus::Success();
        }

        if (emitterState_.queue_ == nullptr || sink_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        shutdownRequested_.store(false, std::memory_order_release);
        emitterState_.shutdown_.store(false, std::memory_order_release);
        sinkStartState_.store(NrSinkStartState::Pending, std::memory_order_release);

        try
        {
            consumerThread_ = std::thread(&NrDiagnosticsComponent::ConsumerLoop, this);
        }
        catch (const std::bad_alloc&)
        {
            sinkStartState_.store(NrSinkStartState::NotStarted, std::memory_order_release);
            CloseEmitterAdmission();
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }
        catch (const std::system_error&)
        {
            sinkStartState_.store(NrSinkStartState::NotStarted, std::memory_order_release);
            CloseEmitterAdmission();
            return NrStatus::Failure(NrErrorCode::IoFailed);
        }

        NrSinkStartState sinkStartState = sinkStartState_.load(std::memory_order_acquire);
        while (sinkStartState == NrSinkStartState::Pending) // wait for Start Success
        {
            sinkStartState_.wait(sinkStartState, std::memory_order_acquire);
            sinkStartState = sinkStartState_.load(std::memory_order_acquire);
        }

        started_ = true;
        if (sinkStartState == NrSinkStartState::Succeeded)
        {
            emitterState_.admissionState_.store(0, std::memory_order_release);
        }
        return NrStatus::Success();
    }

    NrStatus NrDiagnosticsComponent::RequestStop(const NrStopContext& context) noexcept
    {
        static_cast<void>(context);
        return NrStatus::Success();
    }

    NrStatus NrDiagnosticsComponent::Shutdown() noexcept
    {
        if (shutdown_)
        {
            return NrStatus::Success();
        }

        emitterState_.shutdown_.store(true, std::memory_order_release);
        CloseEmitterAdmission();
        shutdownRequested_.store(true, std::memory_order_release);
        WakeConsumer();

        if (consumerThread_.joinable())
        {
            consumerThread_.join();
        }

        emitterState_.queue_.reset();
        sink_.reset();
        configured_ = false;
        started_ = false;
        shutdown_ = true;
        return NrStatus::Success();
    }

    NrDiagnosticEmitter NrDiagnosticsComponent::Emitter() noexcept
    {
        return enabled_ ? NrDiagnosticEmitter(emitterState_) : NrDiagnosticEmitter{};
    }

    NrDiagnosticsStats NrDiagnosticsComponent::CaptureStats() const noexcept
    {
        if (!enabled_)
        {
            return NrDiagnosticsStats{};
        }

        return NrDiagnosticsStats{enabled_,
                                  emitterState_.sinkFailed_.load(std::memory_order_relaxed),
                                  emitterState_.attempted_.load(std::memory_order_relaxed),
                                  emitterState_.enqueued_.load(std::memory_order_relaxed),
                                  emitterState_.droppedQueueFull_.load(std::memory_order_relaxed),
                                  emitterState_.droppedSinkUnavailable_.load(std::memory_order_relaxed),
                                  emitterState_.consumed_.load(std::memory_order_relaxed),
                                  emitterState_.discardedAfterSinkFailure_.load(std::memory_order_relaxed)};
    }

    void NrDiagnosticsComponent::IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept
    {
        constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();

        std::uint64_t observed = counter.load(std::memory_order_relaxed);
        while (observed != MaxValue && !counter.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed,
                                                                      std::memory_order_relaxed))
        {
        }
    }

    void NrDiagnosticsComponent::CloseEmitterAdmission() noexcept
    {
        // set High Bit 1
        emitterState_.admissionState_.fetch_or(NrDiagnosticEmitterState::EmitterAdmissionClosed,
                                               std::memory_order_release);
    }

    void NrDiagnosticsComponent::WaitForEmitterQuiescence() noexcept
    {
        std::uint64_t state = emitterState_.admissionState_.load(std::memory_order_acquire);
        while ((state & NrDiagnosticEmitterState::EmitterCountMask) != 0)
        {
            emitterState_.admissionState_.wait(state, std::memory_order_acquire);
            state = emitterState_.admissionState_.load(std::memory_order_acquire);
        }
    }

    void NrDiagnosticsComponent::WakeConsumer() noexcept
    {
        emitterState_.wakeVersion_.fetch_add(1, std::memory_order_release);
        emitterState_.wakeVersion_.notify_one();
    }

    void NrDiagnosticsComponent::ConsumerLoop() noexcept
    {
        const NrDiagnosticRunMetadata metadata{mode_};
        if (sink_ == nullptr || sink_->Begin(metadata).Failed())
        {
            emitterState_.sinkFailed_.store(true, std::memory_order_release);
            sinkStartState_.store(NrSinkStartState::Failed, std::memory_order_release);
            sinkStartState_.notify_one();
            return;
        }

        sinkStartState_.store(NrSinkStartState::Succeeded, std::memory_order_release); // Start Success
        sinkStartState_.notify_one();

        while (true)
        {
            DrainAvailable();

            const bool sinkFailed = emitterState_.sinkFailed_.load(std::memory_order_acquire);
            if (sinkFailed || shutdownRequested_.load(std::memory_order_acquire))
            {
                WaitForEmitterQuiescence();
                DrainAvailable();

                if (!emitterState_.sinkFailed_.load(std::memory_order_acquire) && sink_ != nullptr &&
                    sink_->Finish(BuildSummary()).Failed())
                {
                    emitterState_.sinkFailed_.store(true, std::memory_order_release);
                    CloseEmitterAdmission();
                }
                return;
            }

            const std::uint64_t observedWakeVersion = emitterState_.wakeVersion_.load(std::memory_order_acquire);
            if ((emitterState_.queue_ != nullptr && emitterState_.queue_->SizeApprox() > 0) ||
                shutdownRequested_.load(std::memory_order_acquire))
            {
                continue;
            }

            emitterState_.wakeVersion_.wait(observedWakeVersion, std::memory_order_acquire);
        }
    }

    void NrDiagnosticsComponent::DrainAvailable() noexcept
    {
        if (emitterState_.queue_ == nullptr)
        {
            return;
        }

        NrDiagnosticRecord record;
        while (emitterState_.queue_->TryPop(record).Succeeded())
        {
            record.drainSequence = nextDrainSequence_;
            if (nextDrainSequence_ != std::numeric_limits<std::uint64_t>::max())
            {
                ++nextDrainSequence_;
            }

            if (emitterState_.sinkFailed_.load(std::memory_order_acquire) || sink_ == nullptr)
            {
                IncrementSaturating(emitterState_.discardedAfterSinkFailure_);
                continue;
            }

            if (sink_->Consume(record).Failed())
            {
                emitterState_.sinkFailed_.store(true, std::memory_order_release);
                CloseEmitterAdmission();
                IncrementSaturating(emitterState_.discardedAfterSinkFailure_);
                continue;
            }

            IncrementSaturating(emitterState_.consumed_);
        }
    }

    NrDiagnosticSummary NrDiagnosticsComponent::BuildSummary() const noexcept
    {
        return NrDiagnosticSummary{
            emitterState_.attempted_.load(std::memory_order_relaxed),
            emitterState_.enqueued_.load(std::memory_order_relaxed),
            emitterState_.droppedQueueFull_.load(std::memory_order_relaxed),
            emitterState_.droppedSinkUnavailable_.load(std::memory_order_relaxed),
            emitterState_.consumed_.load(std::memory_order_relaxed),
            emitterState_.discardedAfterSinkFailure_.load(std::memory_order_relaxed),
        };
    }
} // namespace psnr::runtime::internal
