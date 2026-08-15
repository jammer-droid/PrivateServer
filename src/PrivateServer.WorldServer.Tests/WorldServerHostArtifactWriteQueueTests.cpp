#include "pch.h"

#include "WorldServerHostArtifactWriteQueue.h"

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace psnr::world::host::tests
{
    namespace
    {
        [[nodiscard]] std::unique_ptr<WorldTickSampleBuffer> CreateOwnedSampleBuffer(const std::uint32_t roundId)
        {
            WorldResult<std::unique_ptr<WorldTickSampleBuffer>> result = WorldTickSampleBuffer::Create(1);
            EXPECT_TRUE(result.Succeeded());
            if (result.Failed())
            {
                return nullptr;
            }

            std::unique_ptr<WorldTickSampleBuffer> samples = result.TakeValue();
            EXPECT_TRUE(samples->TryRecord(
                WorldTickSample{roundId, roundId, roundId, roundId, WorldRoundPhase::Running, 1, 1, 0, 1}));
            return samples;
        }

        [[nodiscard]] std::unique_ptr<WorldServerHostArtifactWriteQueue> CreateQueue(const std::size_t tickCapacity,
                                                                                     const std::size_t runtimeCapacity)
        {
            WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> result =
                WorldServerHostArtifactWriteQueue::Create(tickCapacity, runtimeCapacity);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }
    } // namespace

    TEST(WorldServerHostArtifactWriteQueueTests, RejectsZeroCapacityInEitherLane)
    {
        const WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> zeroTickResult =
            WorldServerHostArtifactWriteQueue::Create(0, 1);
        const WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> zeroRuntimeResult =
            WorldServerHostArtifactWriteQueue::Create(1, 0);

        ASSERT_TRUE(zeroTickResult.Failed());
        ASSERT_TRUE(zeroRuntimeResult.Failed());
        EXPECT_EQ(zeroTickResult.Error(), WorldErrorCode::InvalidCapacity);
        EXPECT_EQ(zeroRuntimeResult.Error(), WorldErrorCode::InvalidCapacity);
    }

    TEST(WorldServerHostArtifactWriteQueueTests, PreservesIndependentLaneCapacityAndFifoOrder)
    {
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue(2, 2);
        ASSERT_NE(queue, nullptr);
        std::unique_ptr<WorldTickSampleBuffer> tick11 = CreateOwnedSampleBuffer(11);
        std::unique_ptr<WorldTickSampleBuffer> tick12 = CreateOwnedSampleBuffer(12);
        WorldServerHostRuntimeSample runtime21;
        runtime21.sequence = 21;
        WorldServerHostRuntimeSample runtime22;
        runtime22.sequence = 22;

        ASSERT_EQ(queue->TrySubmit(std::move(tick11), WorldTickSampleBatchCompleteness::Complete),
                  WorldTickSampleSinkResult::Succeeded);
        ASSERT_EQ(queue->TryPushRuntimeSample(std::move(runtime21)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);
        ASSERT_EQ(queue->TrySubmit(std::move(tick12), WorldTickSampleBatchCompleteness::Incomplete),
                  WorldTickSampleSinkResult::Succeeded);
        ASSERT_EQ(queue->TryPushRuntimeSample(std::move(runtime22)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);

        WorldTickSampleBatch firstTick;
        WorldTickSampleBatch secondTick;
        WorldServerHostRuntimeSample firstRuntime;
        WorldServerHostRuntimeSample secondRuntime;
        ASSERT_EQ(queue->TryPopRuntimeSample(&firstRuntime), WorldServerHostArtifactWriteQueueResult::Succeeded);
        ASSERT_EQ(queue->TryPopTickBatch(&firstTick), WorldServerHostArtifactWriteQueueResult::Succeeded);
        ASSERT_EQ(queue->TryPopRuntimeSample(&secondRuntime), WorldServerHostArtifactWriteQueueResult::Succeeded);
        ASSERT_EQ(queue->TryPopTickBatch(&secondTick), WorldServerHostArtifactWriteQueueResult::Succeeded);

        EXPECT_EQ(firstRuntime.sequence, 21u);
        EXPECT_EQ(secondRuntime.sequence, 22u);
        EXPECT_EQ(firstTick.samples->Samples()[0].roundId, 11u);
        EXPECT_EQ(firstTick.completeness, WorldTickSampleBatchCompleteness::Complete);
        EXPECT_EQ(secondTick.samples->Samples()[0].roundId, 12u);
        EXPECT_EQ(secondTick.completeness, WorldTickSampleBatchCompleteness::Incomplete);
    }

    TEST(WorldServerHostArtifactWriteQueueTests, FullLaneDoesNotTakeIncomingOwnershipOrBlockOtherLane)
    {
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue(1, 1);
        ASSERT_NE(queue, nullptr);
        std::unique_ptr<WorldTickSampleBuffer> firstTick = CreateOwnedSampleBuffer(11);
        std::unique_ptr<WorldTickSampleBuffer> retainedTick = CreateOwnedSampleBuffer(12);
        WorldServerHostRuntimeSample runtimeSample;
        runtimeSample.sequence = 21;

        ASSERT_EQ(queue->TrySubmit(std::move(firstTick), WorldTickSampleBatchCompleteness::Complete),
                  WorldTickSampleSinkResult::Succeeded);
        EXPECT_EQ(queue->TrySubmit(std::move(retainedTick), WorldTickSampleBatchCompleteness::Complete),
                  WorldTickSampleSinkResult::Full);
        EXPECT_NE(retainedTick, nullptr);
        EXPECT_EQ(queue->TryPushRuntimeSample(std::move(runtimeSample)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);
    }

    TEST(WorldServerHostArtifactWriteQueueTests, CloseAllowsBothLanesToDrainBeforeReportingClosed)
    {
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue(1, 1);
        ASSERT_NE(queue, nullptr);
        std::unique_ptr<WorldTickSampleBuffer> tick = CreateOwnedSampleBuffer(11);
        WorldServerHostRuntimeSample runtimeSample;
        runtimeSample.sequence = 21;
        ASSERT_EQ(queue->TrySubmit(std::move(tick), WorldTickSampleBatchCompleteness::Complete),
                  WorldTickSampleSinkResult::Succeeded);
        ASSERT_EQ(queue->TryPushRuntimeSample(std::move(runtimeSample)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);

        queue->Close();

        WorldServerHostRuntimeSample receivedRuntime;
        WorldTickSampleBatch receivedTick;
        WorldTickSampleBatch closedTick;
        EXPECT_EQ(queue->WaitForWork(), WorldServerHostArtifactWriteQueueResult::Succeeded);
        EXPECT_EQ(queue->TryPopRuntimeSample(&receivedRuntime), WorldServerHostArtifactWriteQueueResult::Succeeded);
        EXPECT_EQ(queue->TryPopTickBatch(&receivedTick), WorldServerHostArtifactWriteQueueResult::Succeeded);
        EXPECT_EQ(queue->WaitForWork(), WorldServerHostArtifactWriteQueueResult::Closed);
        EXPECT_EQ(queue->TryPopTickBatch(&closedTick), WorldServerHostArtifactWriteQueueResult::Closed);
    }

    TEST(WorldServerHostArtifactWriteQueueTests, RuntimeSampleWakesWaitingConsumer)
    {
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue(1, 1);
        ASSERT_NE(queue, nullptr);
        std::atomic<bool> waiterEntered = false;
        WorldServerHostArtifactWriteQueueResult waitResult = WorldServerHostArtifactWriteQueueResult::InvalidArgument;
        std::thread waiter{
            [&queue, &waiterEntered, &waitResult]() noexcept
            {
                waiterEntered.store(true, std::memory_order_release);
                waitResult = queue->WaitForWork();
            },
        };
        while (!waiterEntered.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        WorldServerHostRuntimeSample runtimeSample;
        runtimeSample.sequence = 21;
        ASSERT_EQ(queue->TryPushRuntimeSample(std::move(runtimeSample)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);
        waiter.join();

        EXPECT_EQ(waitResult, WorldServerHostArtifactWriteQueueResult::Succeeded);
    }
} // namespace psnr::world::host::tests
