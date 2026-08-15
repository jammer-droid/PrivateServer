#include "pch.h"

#include "WorldDoubleBufferedWorkers.h"

#include <limits>
#include <system_error>
#include <utility>

namespace psnr::world
{
    WorldDoubleBufferedCoordinatorWorker::WorldDoubleBufferedCoordinatorWorker(
        const WorldDoubleBufferedCoordinatorWorkerConfig& config, WorldDoubleBufferedTickCoordinator& coordinator,
        WorldIngressDoubleBuffer& ingressBuffer, WorldIngressEventConsumer& eventConsumer,
        WorldIngressWorkerExchange& ingressExchange, std::unique_ptr<WorldTickSampleBuffer> tickSampleBuffer,
        IWorldTickSampleSink* const tickSampleSink) noexcept
        : config_(config)
        , coordinator_(coordinator)
        , ingressBuffer_(ingressBuffer)
        , eventConsumer_(eventConsumer)
        , ingressExchange_(ingressExchange)
        , tickSampleBuffer_(std::move(tickSampleBuffer))
        , tickSampleSink_(tickSampleSink)
    {
    }

    WorldDoubleBufferedCoordinatorWorker::~WorldDoubleBufferedCoordinatorWorker()
    {
        RequestStop();
        Join();
    }

    bool WorldDoubleBufferedCoordinatorWorker::Start() noexcept
    {
        const bool incompleteTickSampleWiring = (tickSampleBuffer_ == nullptr) != (tickSampleSink_ == nullptr);
        if (config_.maxShutdownDrainWait.count() < 0 || incompleteTickSampleWiring ||
            started_.exchange(true, std::memory_order_acq_rel))
        {
            return false;
        }

        try
        {
            stopReason_.store(WorldConcreteWorkerStopReason::Running, std::memory_order_release);
            thread_ = std::thread{&WorldDoubleBufferedCoordinatorWorker::Run, this};
        }
        catch (const std::system_error&)
        {
            started_.store(false, std::memory_order_release);
            stopReason_.store(WorldConcreteWorkerStopReason::StartFailed, std::memory_order_release);
            return false;
        }
        return true;
    }

    void WorldDoubleBufferedCoordinatorWorker::RequestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        ingressBuffer_.Close();
        ingressExchange_.Close();
        coordinator_.CloseOutbound();
        lifecycleCondition_.notify_all();
    }

    bool WorldDoubleBufferedCoordinatorWorker::StopGameplayAndWait() noexcept
    {
        gameplayStopRequested_.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock{lifecycleMutex_};
        return lifecycleCondition_.wait_for(
            lock, config_.maxShutdownDrainWait,
            [this]() noexcept
            {
                return gameplayStopped_.load(std::memory_order_acquire) ||
                       stopReason_.load(std::memory_order_acquire) == WorldConcreteWorkerStopReason::OperationFailed ||
                       stopReason_.load(std::memory_order_acquire) == WorldConcreteWorkerStopReason::StopRequested ||
                       stopReason_.load(std::memory_order_acquire) == WorldConcreteWorkerStopReason::Completed ||
                       stopReason_.load(std::memory_order_acquire) == WorldConcreteWorkerStopReason::StartFailed;
            });
    }

    void WorldDoubleBufferedCoordinatorWorker::EnterTerminalConsume() noexcept
    {
        {
            std::lock_guard<std::mutex> lock{lifecycleMutex_};
            terminalDeadline_ = clockSource_.Now() + config_.maxShutdownDrainWait;
            terminalConsumeEnabled_.store(true, std::memory_order_release);
        }
        lifecycleCondition_.notify_all();
    }

    void WorldDoubleBufferedCoordinatorWorker::Join() noexcept
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    WorldConcreteWorkerStopReason WorldDoubleBufferedCoordinatorWorker::StopReason() const noexcept
    {
        return stopReason_.load(std::memory_order_acquire);
    }

    bool WorldDoubleBufferedCoordinatorWorker::TerminalConsumeSucceeded() const noexcept
    {
        return terminalConsumeEnabled_.load(std::memory_order_acquire) &&
               stopReason_.load(std::memory_order_acquire) == WorldConcreteWorkerStopReason::Completed;
    }

    WorldDoubleBufferedTickReport WorldDoubleBufferedCoordinatorWorker::LastTickReport() const noexcept
    {
        std::lock_guard<std::mutex> lock{reportMutex_};
        return lastTickReport_;
    }

    WorldIngressTerminalConsumeReport WorldDoubleBufferedCoordinatorWorker::TerminalConsumeReport() const noexcept
    {
        std::lock_guard<std::mutex> lock{reportMutex_};
        return terminalConsumeReport_;
    }

    std::uint64_t WorldDoubleBufferedCoordinatorWorker::TickSampleCollectionFailureCount() const noexcept
    {
        return tickSampleCollectionFailureCount_.load(std::memory_order_acquire);
    }

    void WorldDoubleBufferedCoordinatorWorker::Run() noexcept
    {
        if (!RunTickLoop())
        {
            if (!PublishTickSamples(WorldTickSampleBatchCompleteness::Incomplete))
            {
                tickSampleCollectionFailureCount_.fetch_add(1, std::memory_order_relaxed);
            }
            if (stopRequested_.load(std::memory_order_acquire))
            {
                stopReason_.store(WorldConcreteWorkerStopReason::StopRequested, std::memory_order_release);
            }
            else
            {
                stopReason_.store(WorldConcreteWorkerStopReason::OperationFailed, std::memory_order_release);
            }
            gameplayStopped_.store(true, std::memory_order_release);
            lifecycleCondition_.notify_all();
            ingressExchange_.Close();
            coordinator_.CloseOutbound();
            return;
        }
        gameplayStopped_.store(true, std::memory_order_release);
        lifecycleCondition_.notify_all();
        if (!WaitForTerminalMode())
        {
            if (!PublishTickSamples(WorldTickSampleBatchCompleteness::Incomplete))
            {
                tickSampleCollectionFailureCount_.fetch_add(1, std::memory_order_relaxed);
            }
            stopReason_.store(WorldConcreteWorkerStopReason::StopRequested, std::memory_order_release);
            lifecycleCondition_.notify_all();
            ingressExchange_.Close();
            coordinator_.CloseOutbound();
            return;
        }
        if (!RunTerminalLoop())
        {
            if (!PublishTickSamples(WorldTickSampleBatchCompleteness::Incomplete))
            {
                tickSampleCollectionFailureCount_.fetch_add(1, std::memory_order_relaxed);
            }
            stopReason_.store(WorldConcreteWorkerStopReason::OperationFailed, std::memory_order_release);
            lifecycleCondition_.notify_all();
            ingressExchange_.Close();
            coordinator_.CloseOutbound();
            return;
        }

        if (!PublishTickSamples(WorldTickSampleBatchCompleteness::Incomplete))
        {
            tickSampleCollectionFailureCount_.fetch_add(1, std::memory_order_relaxed);
        }

        stopReason_.store(WorldConcreteWorkerStopReason::Completed, std::memory_order_release);
        lifecycleCondition_.notify_all();
        ingressExchange_.Close();
    }

    bool WorldDoubleBufferedCoordinatorWorker::RunTickLoop() noexcept
    {
        while (!gameplayStopRequested_.load(std::memory_order_acquire) &&
               !stopRequested_.load(std::memory_order_acquire))
        {
            const WorldDoubleBufferedTickPlan plan = coordinator_.NextPlan();
            clockSource_.WaitUntil(plan.sealDeadline);
            if (gameplayStopRequested_.load(std::memory_order_acquire) ||
                stopRequested_.load(std::memory_order_acquire))
            {
                break;
            }

            const WorldResult<void, WorldDoubleBufferRoleExchangeError> swapResult =
                ingressBuffer_.WaitSwap(plan.epoch, config_.maxShutdownDrainWait);
            if (swapResult.Failed())
            {
                return false;
            }

            const WorldRoundRuntimeState roundStateAtTickStart = eventConsumer_.GameplayState().RoundState();
            const WorldDoubleBufferedTickReport tickReport =
                coordinator_.RunNext(eventConsumer_, clockSource_, config_.maxShutdownDrainWait);
            const WorldRoundRuntimeState roundStateAtTickEnd = eventConsumer_.GameplayState().RoundState();
            {
                std::lock_guard<std::mutex> lock{reportMutex_};
                lastTickReport_ = tickReport;
            }
            if (tickReport.stopReason != WorldDoubleBufferedTickStopReason::Completed)
            {
                return false;
            }
            if (roundStateAtTickStart.roundId != 0 && roundStateAtTickStart.roundId == roundStateAtTickEnd.roundId &&
                roundStateAtTickStart.phase == WorldRoundPhase::Running &&
                roundStateAtTickEnd.phase == WorldRoundPhase::Ended)
            {
                RotateCompletedRoundTickSamples();
            }
            if (tickReport.outboundExchangeResult != WorldOutboundDoubleBufferExchangeResult::Exchanged &&
                tickReport.outboundExchangeResult != WorldOutboundDoubleBufferExchangeResult::Empty)
            {
                return false;
            }
        }

        return !stopRequested_.load(std::memory_order_acquire);
    }

    bool WorldDoubleBufferedCoordinatorWorker::PublishTickSamples(
        const WorldTickSampleBatchCompleteness completeness) noexcept
    {
        if (tickSampleBuffer_ == nullptr)
        {
            return true;
        }
        if (tickSampleBuffer_->Empty())
        {
            coordinator_.UnbindTickSampleBuffer();
            tickSampleBuffer_.reset();
            return true;
        }
        if (tickSampleSink_ == nullptr)
        {
            coordinator_.UnbindTickSampleBuffer();
            tickSampleBuffer_.reset();
            return false;
        }

        coordinator_.UnbindTickSampleBuffer();
        const WorldTickSampleSinkResult submitResult =
            tickSampleSink_->TrySubmit(std::move(tickSampleBuffer_), completeness);
        if (submitResult != WorldTickSampleSinkResult::Succeeded)
        {
            tickSampleBuffer_.reset();
            return false;
        }
        return true;
    }

    void WorldDoubleBufferedCoordinatorWorker::RotateCompletedRoundTickSamples() noexcept
    {
        if (tickSampleBuffer_ == nullptr)
        {
            return;
        }

        const std::size_t sampleCapacity = tickSampleBuffer_->MaxSampleCount();
        if (!PublishTickSamples(WorldTickSampleBatchCompleteness::Complete))
        {
            tickSampleCollectionFailureCount_.fetch_add(1, std::memory_order_relaxed);
        }

        WorldResult<std::unique_ptr<WorldTickSampleBuffer>> replacementResult =
            WorldTickSampleBuffer::Create(sampleCapacity);
        if (replacementResult.Failed())
        {
            tickSampleCollectionFailureCount_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        tickSampleBuffer_ = replacementResult.TakeValue();
        coordinator_.BindTickSampleBuffer(tickSampleBuffer_.get());
    }

    bool WorldDoubleBufferedCoordinatorWorker::WaitForTerminalMode() noexcept
    {
        std::unique_lock<std::mutex> lock{lifecycleMutex_};
        lifecycleCondition_.wait(lock,
                                 [this]() noexcept
                                 {
                                     return terminalConsumeEnabled_.load(std::memory_order_acquire) ||
                                            stopRequested_.load(std::memory_order_acquire);
                                 });
        return !stopRequested_.load(std::memory_order_acquire);
    }

    bool WorldDoubleBufferedCoordinatorWorker::RunTerminalLoop() noexcept
    {
        std::uint64_t epoch = coordinator_.NextPlan().epoch;
        while (!stopRequested_.load(std::memory_order_acquire) && RemainingTerminalWait().count() > 0)
        {
            if (ingressExchange_.Publish(WorldIngressWorkerPlan{epoch, terminalDeadline_}) !=
                WorldIngressWorkerExchangeResult::Exchanged)
            {
                return false;
            }

            WorldIngressWorkerCompletion completion;
            if (ingressExchange_.WaitTakeCompletion(epoch, &completion) != WorldIngressWorkerExchangeResult::Exchanged)
            {
                return false;
            }

            const WorldResult<void, WorldDoubleBufferRoleExchangeError> swapResult =
                ingressBuffer_.WaitSwap(epoch, RemainingTerminalWait());
            if (swapResult.Failed())
            {
                return false;
            }

            WorldResult<WorldIngressReadBatch, WorldDoubleBufferRoleExchangeError> readResult =
                ingressBuffer_.WaitAcquireRead(epoch, std::chrono::milliseconds::zero());
            if (readResult.Failed())
            {
                return false;
            }
            WorldIngressReadBatch readBatch = readResult.TakeValue();

            const WorldIngressTerminalConsumeReport consumeReport =
                terminalConsumer_.Consume(readBatch, eventConsumer_);
            if (ingressBuffer_.ReleaseRead(readBatch).Failed())
            {
                return false;
            }
            {
                std::lock_guard<std::mutex> lock{reportMutex_};
                terminalConsumeReport_.acceptedEventCount += consumeReport.acceptedEventCount;
                terminalConsumeReport_.closedEventCount += consumeReport.closedEventCount;
                terminalConsumeReport_.discardedPacketCount += consumeReport.discardedPacketCount;
                terminalConsumeReport_.unsupportedEventCount += consumeReport.unsupportedEventCount;
            }

            if (completion.terminalReport.stopReason == WorldIngressTerminalEpochStopReason::SourceClosed)
            {
                return true;
            }
            if (completion.terminalReport.stopReason != WorldIngressTerminalEpochStopReason::SealedFull ||
                epoch == std::numeric_limits<std::uint64_t>::max())
            {
                return false;
            }
            ++epoch;
        }
        return false;
    }

    std::chrono::milliseconds WorldDoubleBufferedCoordinatorWorker::RemainingTerminalWait() const noexcept
    {
        const WorldClock::time_point now = clockSource_.Now();
        if (now >= terminalDeadline_)
        {
            return std::chrono::milliseconds::zero();
        }

        const std::uint32_t timeoutMilliseconds = CalculateWorldWaitTimeoutMilliseconds(terminalDeadline_ - now);
        return std::chrono::milliseconds{timeoutMilliseconds};
    }
} // namespace psnr::world
