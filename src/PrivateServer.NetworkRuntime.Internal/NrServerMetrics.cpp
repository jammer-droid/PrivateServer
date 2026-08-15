#include "pch.h"

#include "NrServerMetrics.h"
#include "NrServerPressureInternal.h"

#include <cassert>
#include <limits>

namespace psnr::runtime::internal
{
    void NrSaturatingCounter::Increment() noexcept
    {
        constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();

        std::uint64_t observed = value_.load(std::memory_order_relaxed);
        while (observed != MaxValue && !value_.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed,
                                                                     std::memory_order_relaxed))
        {
        }
    }

    std::uint64_t NrSaturatingCounter::Load() const noexcept
    {
        return value_.load(std::memory_order_relaxed);
    }

    void NrServerMetrics::Record(const NrPressureTransactionOutcome outcome) noexcept
    {
        assert(IsKnownPressureTransactionOutcome(outcome));
        if (!IsKnownPressureTransactionOutcome(outcome))
        {
            return;
        }

        pressureTransactionCounters_[static_cast<std::size_t>(outcome)].Increment();
    }

    void NrServerMetrics::RecordPendingRecvIoStarted() noexcept
    {
        pendingRecvIoCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void NrServerMetrics::RecordPendingRecvIoCompleted() noexcept
    {
        DecrementPendingIo(pendingRecvIoCount_);
    }

    void NrServerMetrics::RecordPendingSendIoStarted() noexcept
    {
        pendingSendIoCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void NrServerMetrics::RecordPendingSendIoCompleted() noexcept
    {
        DecrementPendingIo(pendingSendIoCount_);
    }

    std::uint64_t NrServerMetrics::BeginSendMailboxEnqueue() noexcept
    {
        return sendMailboxDepth_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    void NrServerMetrics::CommitSendMailboxEnqueue(const std::uint64_t admittedDepth) noexcept
    {
        UpdateHighWatermark(sendMailboxHighWatermark_, admittedDepth);
    }

    void NrServerMetrics::CancelSendMailboxEnqueue() noexcept
    {
        DecrementDepth(sendMailboxDepth_);
    }

    void NrServerMetrics::RecordSendMailboxDequeued() noexcept
    {
        DecrementDepth(sendMailboxDepth_);
    }

    void NrServerMetrics::RecordPendingSendQueued() noexcept
    {
        const std::uint64_t depth = pendingSendQueueDepth_.fetch_add(1, std::memory_order_relaxed) + 1;
        UpdateHighWatermark(pendingSendQueueHighWatermark_, depth);
    }

    void NrServerMetrics::RecordPendingSendDequeued() noexcept
    {
        DecrementDepth(pendingSendQueueDepth_);
    }

    void NrServerMetrics::DecrementPendingIo(std::atomic<std::uint64_t>& count) noexcept
    {
        std::uint64_t observed = count.load(std::memory_order_relaxed);
        while (observed != 0)
        {
            if (count.compare_exchange_weak(observed, observed - 1, std::memory_order_relaxed,
                                            std::memory_order_relaxed))
            {
                return;
            }
        }

        assert(false && "pending IO count underflow");
    }

    void NrServerMetrics::DecrementDepth(std::atomic<std::uint64_t>& depth) noexcept
    {
        std::uint64_t observed = depth.load(std::memory_order_relaxed);
        while (observed != 0)
        {
            if (depth.compare_exchange_weak(observed, observed - 1, std::memory_order_relaxed,
                                            std::memory_order_relaxed))
            {
                return;
            }
        }

        assert(false && "send backlog depth underflow");
    }

    void NrServerMetrics::UpdateHighWatermark(std::atomic<std::uint64_t>& highWatermark,
                                              const std::uint64_t observedDepth) noexcept
    {
        std::uint64_t observedHighWatermark = highWatermark.load(std::memory_order_relaxed);
        while (observedHighWatermark < observedDepth &&
               !highWatermark.compare_exchange_weak(observedHighWatermark, observedDepth, std::memory_order_relaxed,
                                                    std::memory_order_relaxed))
        {
        }
    }

    NrServerMetricsSnapshot NrServerMetrics::Capture() const noexcept
    {
        NrServerMetricsSnapshot snapshot;

        for (std::size_t index = 0; index < NrPressureTransactionOutcomeCount; ++index)
        {
            snapshot.pressureTransactionCounts[index] = pressureTransactionCounters_[index].Load();
        }

        snapshot.pendingRecvIoCount = pendingRecvIoCount_.load(std::memory_order_relaxed);
        snapshot.pendingSendIoCount = pendingSendIoCount_.load(std::memory_order_relaxed);
        snapshot.sendMailboxDepth = sendMailboxDepth_.load(std::memory_order_relaxed);
        snapshot.sendMailboxHighWatermark = sendMailboxHighWatermark_.load(std::memory_order_relaxed);
        snapshot.pendingSendQueueDepth = pendingSendQueueDepth_.load(std::memory_order_relaxed);
        snapshot.pendingSendQueueHighWatermark = pendingSendQueueHighWatermark_.load(std::memory_order_relaxed);

        return snapshot;
    }
} // namespace psnr::runtime::internal
