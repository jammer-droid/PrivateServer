#include "pch.h"

#include "NrErrorCode.h"
#include "NrServerComponentGraph.h"
#include "NrServerMetrics.h"

#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace psnr::runtime::internal
{
    class NrServerMetricsTestAccess final
    {
    public:
        static void Set(NrSaturatingCounter& counter, const std::uint64_t value) noexcept
        {
            counter.value_.store(value, std::memory_order_relaxed);
        }
    };

    namespace
    {
        TEST(NrServerMetricsTests, NewMetricsStartWithZeroCounters)
        {
            const NrServerMetrics metrics;

            const NrServerMetricsSnapshot snapshot = metrics.Capture();

            for (std::size_t index = 0; index < NrPressureTransactionOutcomeCount; ++index)
            {
                EXPECT_EQ(snapshot.pressureTransactionCounts[index], 0u);
            }
            EXPECT_EQ(snapshot.sendMailboxDepth, 0u);
            EXPECT_EQ(snapshot.sendMailboxHighWatermark, 0u);
            EXPECT_EQ(snapshot.pendingSendQueueDepth, 0u);
            EXPECT_EQ(snapshot.pendingSendQueueHighWatermark, 0u);
        }

        TEST(NrServerMetricsTests, RecordsTransactionDimensionsIndependently)
        {
            NrServerMetrics metrics;

            metrics.Record(NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);
            metrics.Record(NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);
            metrics.Record(NrPressureTransactionOutcome::SendAdmissionRejected);

            const NrServerMetricsSnapshot snapshot = metrics.Capture();

            EXPECT_EQ(snapshot.pressureTransactionCounts[static_cast<std::size_t>(
                          NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected)],
                      2u);
            EXPECT_EQ(snapshot.pressureTransactionCounts[static_cast<std::size_t>(
                          NrPressureTransactionOutcome::SendAdmissionRejected)],
                      1u);
        }

        TEST(NrServerMetricsTests, TracksPendingRecvAndSendIoIndependently)
        {
            NrServerMetrics metrics;

            metrics.RecordPendingRecvIoStarted();
            metrics.RecordPendingRecvIoStarted();
            metrics.RecordPendingSendIoStarted();

            NrServerMetricsSnapshot snapshot = metrics.Capture();
            EXPECT_EQ(snapshot.pendingRecvIoCount, 2u);
            EXPECT_EQ(snapshot.pendingSendIoCount, 1u);

            metrics.RecordPendingRecvIoCompleted();
            metrics.RecordPendingSendIoCompleted();

            snapshot = metrics.Capture();
            EXPECT_EQ(snapshot.pendingRecvIoCount, 1u);
            EXPECT_EQ(snapshot.pendingSendIoCount, 0u);
        }

        TEST(NrServerMetricsTests, SaturatingCounterStopsAtUint64Maximum)
        {
            NrSaturatingCounter counter;
            constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();
            NrServerMetricsTestAccess::Set(counter, MaxValue - 1);

            counter.Increment();
            counter.Increment();

            EXPECT_EQ(counter.Load(), MaxValue);
        }

        TEST(NrServerMetricsTests, TracksSendMailboxAdmissionDepthAndHighWatermark)
        {
            NrServerMetrics metrics;

            const std::uint64_t firstDepth = metrics.BeginSendMailboxEnqueue();
            metrics.CommitSendMailboxEnqueue(firstDepth);
            const std::uint64_t secondDepth = metrics.BeginSendMailboxEnqueue();
            metrics.CommitSendMailboxEnqueue(secondDepth);
            const std::uint64_t rejectedDepth = metrics.BeginSendMailboxEnqueue();
            metrics.CancelSendMailboxEnqueue();
            metrics.RecordSendMailboxDequeued();

            NrServerMetricsSnapshot snapshot = metrics.Capture();
            EXPECT_EQ(rejectedDepth, 3u);
            EXPECT_EQ(snapshot.sendMailboxDepth, 1u);
            EXPECT_EQ(snapshot.sendMailboxHighWatermark, 2u);

            metrics.RecordSendMailboxDequeued();
            snapshot = metrics.Capture();
            EXPECT_EQ(snapshot.sendMailboxDepth, 0u);
            EXPECT_EQ(snapshot.sendMailboxHighWatermark, 2u);
        }

        TEST(NrServerMetricsTests, TracksPendingSendQueueDepthAndHighWatermark)
        {
            NrServerMetrics metrics;

            metrics.RecordPendingSendQueued();
            metrics.RecordPendingSendQueued();
            metrics.RecordPendingSendDequeued();

            NrServerMetricsSnapshot snapshot = metrics.Capture();
            EXPECT_EQ(snapshot.pendingSendQueueDepth, 1u);
            EXPECT_EQ(snapshot.pendingSendQueueHighWatermark, 2u);

            metrics.RecordPendingSendDequeued();
            snapshot = metrics.Capture();
            EXPECT_EQ(snapshot.pendingSendQueueDepth, 0u);
            EXPECT_EQ(snapshot.pendingSendQueueHighWatermark, 2u);
        }

        TEST(NrServerMetricsTests, ConcurrentRecordsAreNotLost)
        {
            constexpr std::size_t ThreadCount = 8;
            constexpr std::size_t RecordsPerThread = 10000;
            NrServerMetrics metrics;
            std::vector<std::thread> threads;
            threads.reserve(ThreadCount);

            for (std::size_t index = 0; index < ThreadCount; ++index)
            {
                threads.emplace_back(
                    [&metrics]()
                    {
                        for (std::size_t recordIndex = 0; recordIndex < RecordsPerThread; ++recordIndex)
                        {
                            metrics.Record(NrPressureTransactionOutcome::ActorAdmissionReadyCapacityRejected);
                        }
                    });
            }

            for (std::thread& thread : threads)
            {
                thread.join();
            }

            const NrServerMetricsSnapshot snapshot = metrics.Capture();
            EXPECT_EQ(snapshot.pressureTransactionCounts[static_cast<std::size_t>(
                          NrPressureTransactionOutcome::ActorAdmissionReadyCapacityRejected)],
                      ThreadCount * RecordsPerThread);
        }

        TEST(NrServerMetricsTests, ComponentGraphOwnsOneMetricsInstance)
        {
            NrServerComponentGraph graph;
            EXPECT_EQ(graph.Metrics(), nullptr);

            ASSERT_TRUE(graph.InitializeMetrics().Succeeded());
            NrServerMetrics* metrics = graph.Metrics();
            ASSERT_NE(metrics, nullptr);
            metrics->Record(NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);

            const psnr::core::NrStatus repeatedInitializeStatus = graph.InitializeMetrics();
            EXPECT_EQ(repeatedInitializeStatus.ErrorCode(), psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(graph.Metrics(), metrics);

            NrServerComponentGraph movedGraph(std::move(graph));
            EXPECT_EQ(graph.Metrics(), nullptr);
            ASSERT_NE(movedGraph.Metrics(), nullptr);

            const NrServerMetricsSnapshot snapshot = movedGraph.Metrics()->Capture();
            EXPECT_EQ(snapshot.pressureTransactionCounts[static_cast<std::size_t>(
                          NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected)],
                      1u);
        }
    } // namespace
} // namespace psnr::runtime::internal
