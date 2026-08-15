#pragma once

#include "NrServerPressureTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace psnr::runtime::internal
{
    class NrServerMetricsTestAccess;

    class NrSaturatingCounter final
    {
        friend class NrServerMetricsTestAccess;

    public:
        NrSaturatingCounter() noexcept = default;

        NrSaturatingCounter(const NrSaturatingCounter&) = delete;
        NrSaturatingCounter& operator=(const NrSaturatingCounter&) = delete;
        NrSaturatingCounter(NrSaturatingCounter&&) = delete;
        NrSaturatingCounter& operator=(NrSaturatingCounter&&) = delete;

        void Increment() noexcept;

        [[nodiscard]] std::uint64_t Load() const noexcept;

    private:
        std::atomic<std::uint64_t> value_{0};
    };

    struct NrServerMetricsSnapshot final
    {
        std::uint64_t pressureTransactionCounts[NrPressureTransactionOutcomeCount]{};
        std::uint64_t pendingRecvIoCount = 0;
        std::uint64_t pendingSendIoCount = 0;
        std::uint64_t sendMailboxDepth = 0;
        std::uint64_t sendMailboxHighWatermark = 0;
        std::uint64_t pendingSendQueueDepth = 0;
        std::uint64_t pendingSendQueueHighWatermark = 0;
    };

    class NrServerMetrics final
    {
    public:
        NrServerMetrics() noexcept = default;

        NrServerMetrics(const NrServerMetrics&) = delete;
        NrServerMetrics& operator=(const NrServerMetrics&) = delete;
        NrServerMetrics(NrServerMetrics&&) = delete;
        NrServerMetrics& operator=(NrServerMetrics&&) = delete;

        void Record(NrPressureTransactionOutcome outcome) noexcept;
        void RecordPendingRecvIoStarted() noexcept;
        void RecordPendingRecvIoCompleted() noexcept;
        void RecordPendingSendIoStarted() noexcept;
        void RecordPendingSendIoCompleted() noexcept;
        [[nodiscard]] std::uint64_t BeginSendMailboxEnqueue() noexcept;
        void CommitSendMailboxEnqueue(std::uint64_t admittedDepth) noexcept;
        void CancelSendMailboxEnqueue() noexcept;
        void RecordSendMailboxDequeued() noexcept;
        void RecordPendingSendQueued() noexcept;
        void RecordPendingSendDequeued() noexcept;

        [[nodiscard]] NrServerMetricsSnapshot Capture() const noexcept;

    private:
        static void DecrementPendingIo(std::atomic<std::uint64_t>& count) noexcept;
        static void DecrementDepth(std::atomic<std::uint64_t>& depth) noexcept;
        static void UpdateHighWatermark(std::atomic<std::uint64_t>& highWatermark,
                                        std::uint64_t observedDepth) noexcept;

        NrSaturatingCounter pressureTransactionCounters_[NrPressureTransactionOutcomeCount];
        std::atomic<std::uint64_t> pendingRecvIoCount_{0};
        std::atomic<std::uint64_t> pendingSendIoCount_{0};
        std::atomic<std::uint64_t> sendMailboxDepth_{0};
        std::atomic<std::uint64_t> sendMailboxHighWatermark_{0};
        std::atomic<std::uint64_t> pendingSendQueueDepth_{0};
        std::atomic<std::uint64_t> pendingSendQueueHighWatermark_{0};
    };
} // namespace psnr::runtime::internal
