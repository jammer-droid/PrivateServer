#pragma once

#include "IWorldTickSampleSink.h"
#include "WorldResult.h"
#include "WorldServerHostRuntimeSample.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace psnr::world::host
{
    enum class WorldServerHostArtifactWriteQueueResult : std::uint8_t
    {
        Succeeded = 0,
        Full,
        Empty,
        Closed,
        InvalidArgument,
    };

    // 하나의 동기화 지점에서 하나의 writer thread를 깨우되, 두 bounded lane은 각각의
    // capacity와 FIFO 순서를 독립적으로 유지한다. pop은 mutex를 잡은 상태에서 queue slot을
    // 이동하고 비우며, 파일 I/O는 writer가 반환값의 소유권을 받은 뒤 lock 밖에서 수행해야 한다.
    class WorldServerHostArtifactWriteQueue final : public IWorldTickSampleSink
    {
    public:
        [[nodiscard]] static WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> Create(
            std::size_t maxPendingTickBatchCount, std::size_t maxPendingRuntimeSampleCount) noexcept;

        WorldServerHostArtifactWriteQueue(const WorldServerHostArtifactWriteQueue&) = delete;
        WorldServerHostArtifactWriteQueue& operator=(const WorldServerHostArtifactWriteQueue&) = delete;
        WorldServerHostArtifactWriteQueue(WorldServerHostArtifactWriteQueue&&) = delete;
        WorldServerHostArtifactWriteQueue& operator=(WorldServerHostArtifactWriteQueue&&) = delete;

        [[nodiscard]] WorldTickSampleSinkResult TrySubmit(
            std::unique_ptr<WorldTickSampleBuffer>&& samples,
            WorldTickSampleBatchCompleteness completeness) noexcept override;
        [[nodiscard]] WorldServerHostArtifactWriteQueueResult TryPushRuntimeSample(
            WorldServerHostRuntimeSample&& sample) noexcept;

        [[nodiscard]] WorldServerHostArtifactWriteQueueResult WaitForWork() noexcept;
        [[nodiscard]] WorldServerHostArtifactWriteQueueResult TryPopTickBatch(WorldTickSampleBatch* outBatch) noexcept;
        [[nodiscard]] WorldServerHostArtifactWriteQueueResult TryPopRuntimeSample(
            WorldServerHostRuntimeSample* outSample) noexcept;
        void Close() noexcept;

        [[nodiscard]] std::size_t MaxPendingTickBatchCount() const noexcept;
        [[nodiscard]] std::size_t MaxPendingRuntimeSampleCount() const noexcept;
        [[nodiscard]] std::size_t PendingTickBatchCount() const noexcept;
        [[nodiscard]] std::size_t PendingRuntimeSampleCount() const noexcept;

    private:
        WorldServerHostArtifactWriteQueue() noexcept = default;

        [[nodiscard]] bool HasPendingWork() const noexcept;

        mutable std::mutex mutex_;
        std::condition_variable condition_;

        // 두 lane은 의도적으로 slot을 공유하지 않는다. round batch 기록이 느려져도
        // 1초 주기 Runtime sample용 capacity를 덮어쓰거나 소비할 수 없다.
        std::vector<WorldTickSampleBatch> tickBatchSlots_;
        std::size_t tickBatchReadIndex_ = 0;
        std::size_t tickBatchWriteIndex_ = 0;
        std::size_t pendingTickBatchCount_ = 0;

        std::vector<WorldServerHostRuntimeSample> runtimeSampleSlots_;
        std::size_t runtimeSampleReadIndex_ = 0;
        std::size_t runtimeSampleWriteIndex_ = 0;
        std::size_t pendingRuntimeSampleCount_ = 0;

        bool closed_ = false;
    };
} // namespace psnr::world::host
