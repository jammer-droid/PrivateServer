#include "WorldServerHostArtifactWriteQueue.h"

#include <new>
#include <stdexcept>
#include <utility>

namespace psnr::world::host
{
    WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> WorldServerHostArtifactWriteQueue::Create(
        const std::size_t maxPendingTickBatchCount, const std::size_t maxPendingRuntimeSampleCount) noexcept
    {
        if (maxPendingTickBatchCount == 0 || maxPendingRuntimeSampleCount == 0)
        {
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>>::Failure(
                WorldErrorCode::InvalidCapacity);
        }

        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue{new (std::nothrow)
                                                                     WorldServerHostArtifactWriteQueue{}};
        if (queue == nullptr)
        {
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>>::Failure(
                WorldErrorCode::AllocationFailed);
        }

        try
        {
            queue->tickBatchSlots_.resize(maxPendingTickBatchCount);
            queue->runtimeSampleSlots_.resize(maxPendingRuntimeSampleCount);
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>>{std::move(queue)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>>::Failure(
                WorldErrorCode::AllocationFailed);
        }
        catch (const std::length_error&)
        {
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>>::Failure(
                WorldErrorCode::InvalidCapacity);
        }
    }

    WorldTickSampleSinkResult WorldServerHostArtifactWriteQueue::TrySubmit(
        std::unique_ptr<WorldTickSampleBuffer>&& samples, const WorldTickSampleBatchCompleteness completeness) noexcept
    {
        if (samples == nullptr || (completeness != WorldTickSampleBatchCompleteness::Complete &&
                                   completeness != WorldTickSampleBatchCompleteness::Incomplete))
        {
            return WorldTickSampleSinkResult::InvalidArgument;
        }

        {
            std::lock_guard<std::mutex> lock{mutex_};
            if (closed_)
            {
                return WorldTickSampleSinkResult::Closed;
            }
            if (pendingTickBatchCount_ == tickBatchSlots_.size())
            {
                return WorldTickSampleSinkResult::Full;
            }

            tickBatchSlots_[tickBatchWriteIndex_] = WorldTickSampleBatch{std::move(samples), completeness};
            tickBatchWriteIndex_ = (tickBatchWriteIndex_ + 1) % tickBatchSlots_.size();
            ++pendingTickBatchCount_;
        }
        condition_.notify_one();
        return WorldTickSampleSinkResult::Succeeded;
    }

    WorldServerHostArtifactWriteQueueResult WorldServerHostArtifactWriteQueue::TryPushRuntimeSample(
        WorldServerHostRuntimeSample&& sample) noexcept
    {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            if (closed_)
            {
                return WorldServerHostArtifactWriteQueueResult::Closed;
            }
            if (pendingRuntimeSampleCount_ == runtimeSampleSlots_.size())
            {
                return WorldServerHostArtifactWriteQueueResult::Full;
            }

            runtimeSampleSlots_[runtimeSampleWriteIndex_] = std::move(sample);
            runtimeSampleWriteIndex_ = (runtimeSampleWriteIndex_ + 1) % runtimeSampleSlots_.size();
            ++pendingRuntimeSampleCount_;
        }
        condition_.notify_one();
        return WorldServerHostArtifactWriteQueueResult::Succeeded;
    }

    WorldServerHostArtifactWriteQueueResult WorldServerHostArtifactWriteQueue::WaitForWork() noexcept
    {
        std::unique_lock<std::mutex> lock{mutex_};
        condition_.wait(lock, [this]() noexcept { return HasPendingWork() || closed_; });
        return HasPendingWork() ? WorldServerHostArtifactWriteQueueResult::Succeeded
                                : WorldServerHostArtifactWriteQueueResult::Closed;
    }

    WorldServerHostArtifactWriteQueueResult WorldServerHostArtifactWriteQueue::TryPopTickBatch(
        WorldTickSampleBatch* const outBatch) noexcept
    {
        if (outBatch == nullptr || outBatch->samples != nullptr)
        {
            return WorldServerHostArtifactWriteQueueResult::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (pendingTickBatchCount_ == 0)
        {
            return closed_ && !HasPendingWork() ? WorldServerHostArtifactWriteQueueResult::Closed
                                                : WorldServerHostArtifactWriteQueueResult::Empty;
        }

        *outBatch = std::move(tickBatchSlots_[tickBatchReadIndex_]);
        tickBatchSlots_[tickBatchReadIndex_] = WorldTickSampleBatch{};
        tickBatchReadIndex_ = (tickBatchReadIndex_ + 1) % tickBatchSlots_.size();
        --pendingTickBatchCount_;
        return WorldServerHostArtifactWriteQueueResult::Succeeded;
    }

    WorldServerHostArtifactWriteQueueResult WorldServerHostArtifactWriteQueue::TryPopRuntimeSample(
        WorldServerHostRuntimeSample* const outSample) noexcept
    {
        if (outSample == nullptr)
        {
            return WorldServerHostArtifactWriteQueueResult::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (pendingRuntimeSampleCount_ == 0)
        {
            return closed_ && !HasPendingWork() ? WorldServerHostArtifactWriteQueueResult::Closed
                                                : WorldServerHostArtifactWriteQueueResult::Empty;
        }

        *outSample = std::move(runtimeSampleSlots_[runtimeSampleReadIndex_]);
        runtimeSampleSlots_[runtimeSampleReadIndex_] = WorldServerHostRuntimeSample{};
        runtimeSampleReadIndex_ = (runtimeSampleReadIndex_ + 1) % runtimeSampleSlots_.size();
        --pendingRuntimeSampleCount_;
        return WorldServerHostArtifactWriteQueueResult::Succeeded;
    }

    void WorldServerHostArtifactWriteQueue::Close() noexcept
    {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            closed_ = true;
        }
        condition_.notify_all();
    }

    std::size_t WorldServerHostArtifactWriteQueue::MaxPendingTickBatchCount() const noexcept
    {
        return tickBatchSlots_.size();
    }

    std::size_t WorldServerHostArtifactWriteQueue::MaxPendingRuntimeSampleCount() const noexcept
    {
        return runtimeSampleSlots_.size();
    }

    std::size_t WorldServerHostArtifactWriteQueue::PendingTickBatchCount() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return pendingTickBatchCount_;
    }

    std::size_t WorldServerHostArtifactWriteQueue::PendingRuntimeSampleCount() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return pendingRuntimeSampleCount_;
    }

    bool WorldServerHostArtifactWriteQueue::HasPendingWork() const noexcept
    {
        return pendingTickBatchCount_ > 0 || pendingRuntimeSampleCount_ > 0;
    }
} // namespace psnr::world::host
