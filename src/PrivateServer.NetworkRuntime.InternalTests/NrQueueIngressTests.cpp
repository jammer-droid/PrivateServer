#include "pch.h"

#include "NrQueueIngress.h"

#include "NrBoundedMpscQueue.h"
#include "NrMemoryPoolTestUtils.h"

#include <cstddef>
#include <memory>
#include <utility>

namespace psnr::core
{
    namespace
    {
        constexpr NrPacketType HeartbeatPacketType{0};

        using NrInputQueue = NrBoundedMpscQueue<NrInput>;

        [[nodiscard]] NrMemoryPoolManagerConfig MakeQueueTestManagerConfig()
        {
            constexpr std::size_t QueueCapacity = 2;
            constexpr std::size_t QueueStorageBytes = sizeof(NrBoundedMpscQueueSlot<NrInput>) * QueueCapacity;

            return test::MakeMemoryPoolManagerConfigWith(
                NrMemoryPoolRole::RuntimeIngressQueueStorage,
                test::MakePoolConfig(QueueStorageBytes, 1, NrCacheLineSize));
        }

        [[nodiscard]] std::unique_ptr<NrMemoryPoolManager> CreateQueueTestManager()
        {
            return test::CreateMemoryPoolManager(MakeQueueTestManagerConfig());
        }

        [[nodiscard]] std::unique_ptr<NrInputQueue> CreateQueue(NrMemoryPoolManager& manager)
        {
            NrResult<std::unique_ptr<NrInputQueue>> queueResult =
                NrInputQueue::Create(manager, NrMemoryPoolRole::RuntimeIngressQueueStorage, 2);
            EXPECT_TRUE(queueResult.Succeeded());
            if (queueResult.Failed())
            {
                return nullptr;
            }

            return queueResult.TakeValue();
        }

        [[nodiscard]] NrInput MakeInput(NrSessionKey sessionId = 1) noexcept
        {
            return NrInput(sessionId, HeartbeatPacketType, NrDispatchLane::ServerIngress, NrPayload{});
        }

        TEST(NrQueueIngressTests, TryEnqueueMovesInputIntoQueue)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateQueueTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);

            NrInput input = MakeInput(42);

            const NrStatus status = ingress.TryEnqueue(std::move(input));

            EXPECT_TRUE(status.Succeeded());
            EXPECT_EQ(queue->SizeApprox(), 1u);

            NrInput output;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.sessionId, 42u);
            EXPECT_EQ(output.packetType, HeartbeatPacketType);
            EXPECT_EQ(output.dispatchLane, NrDispatchLane::ServerIngress);
        }

        TEST(NrQueueIngressTests, TryEnqueuePropagatesQueueFull)
        {
            std::unique_ptr<NrMemoryPoolManager> manager = CreateQueueTestManager();
            ASSERT_NE(manager, nullptr);
            std::unique_ptr<NrInputQueue> queue = CreateQueue(*manager);
            ASSERT_NE(queue, nullptr);
            NrQueueIngress ingress(*queue);

            ASSERT_TRUE(ingress.TryEnqueue(MakeInput(1)).Succeeded());
            ASSERT_TRUE(ingress.TryEnqueue(MakeInput(2)).Succeeded());

            const NrStatus status = ingress.TryEnqueue(MakeInput(3));

            EXPECT_TRUE(status.Failed());
            EXPECT_EQ(status.ErrorCode(), NrErrorCode::QueueFull);
            EXPECT_EQ(queue->SizeApprox(), 2u);

            NrInput output;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.sessionId, 1u);
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.sessionId, 2u);
        }
    } // namespace
} // namespace psnr::core
