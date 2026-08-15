#include "pch.h"

#include "NrServerSnapshot.h"

#include "NrServerSnapshotAccess.h"
#include "NrServerPressureInternal.h"

#include <limits>

namespace psnr::runtime
{
    namespace
    {
        [[nodiscard]] constexpr std::uint64_t SaturatingAdd(const std::uint64_t left,
                                                            const std::uint64_t right) noexcept
        {
            constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();
            return right > MaxValue - left ? MaxValue : left + right;
        }
    } // namespace

    NrServerSnapshot::NrServerSnapshot() noexcept = default;

    NrServerSnapshot::~NrServerSnapshot() noexcept = default;

    bool NrServerSnapshot::IsValid() const noexcept
    {
        return isValid_;
    }

    NrServerLifecycleState NrServerSnapshot::LifecycleState() const noexcept
    {
        return lifecycleState_;
    }

    std::uint64_t NrServerSnapshot::RegisteredSessionCount() const noexcept
    {
        return registeredSessionCount_;
    }

    std::uint64_t NrServerSnapshot::ClosingSessionCount() const noexcept
    {
        return closingSessionCount_;
    }

    std::uint64_t NrServerSnapshot::PendingRecvIoCount() const noexcept
    {
        return pendingRecvIoCount_;
    }

    std::uint64_t NrServerSnapshot::PendingSendIoCount() const noexcept
    {
        return pendingSendIoCount_;
    }

    std::uint64_t NrServerSnapshot::PendingIoCount() const noexcept
    {
        return SaturatingAdd(pendingRecvIoCount_, pendingSendIoCount_);
    }

    std::uint64_t NrServerSnapshot::SendMailboxDepth() const noexcept
    {
        return sendMailboxDepth_;
    }

    std::uint64_t NrServerSnapshot::SendMailboxHighWatermark() const noexcept
    {
        return sendMailboxHighWatermark_;
    }

    std::uint64_t NrServerSnapshot::PendingSendQueueDepth() const noexcept
    {
        return pendingSendQueueDepth_;
    }

    std::uint64_t NrServerSnapshot::PendingSendQueueHighWatermark() const noexcept
    {
        return pendingSendQueueHighWatermark_;
    }

    std::uint64_t NrServerSnapshot::ToWorldEventDepth() const noexcept
    {
        return toWorldEventDepth_;
    }

    std::uint64_t NrServerSnapshot::ToWorldEventHighWatermark() const noexcept
    {
        return toWorldEventHighWatermark_;
    }

    std::uint64_t NrServerSnapshot::PressureTransactionCount(const NrPressureTransactionOutcome outcome) const noexcept
    {
        return internal::IsKnownPressureTransactionOutcome(outcome)
                   ? pressureTransactionCounts_[static_cast<std::size_t>(outcome)]
                   : 0;
    }

    std::uint64_t NrServerSnapshot::PoolAcquirePressureCount(const NrServerMemoryPoolRole role,
                                                             const NrPoolPressureOutcome outcome) const noexcept
    {
        if (!internal::IsKnownServerMemoryPoolRole(role) || !internal::IsKnownPoolPressureOutcome(outcome))
        {
            return 0;
        }

        return poolAcquirePressureCounts_[static_cast<std::size_t>(role)][static_cast<std::size_t>(outcome)];
    }

    std::uint64_t NrServerSnapshot::TotalPressureTransactions() const noexcept
    {
        return totalPressureTransactions_;
    }

    NrServerMemoryPoolSnapshot NrServerSnapshot::MemoryPool(const NrServerMemoryPoolRole role) const noexcept
    {
        return internal::IsKnownServerMemoryPoolRole(role) ? memoryPools_[static_cast<std::size_t>(role)]
                                                           : NrServerMemoryPoolSnapshot{};
    }

    NrServerDiagnosticsSnapshot NrServerSnapshot::Diagnostics() const noexcept
    {
        return diagnostics_;
    }

    void internal::NrServerSnapshotAccess::Assign(const NrServerSnapshotData& data,
                                                  NrServerSnapshot& outSnapshot) noexcept
    {
        NrServerSnapshot snapshot;
        if (data.lifecycleState == NrServerLifecycleState::Invalid)
        {
            outSnapshot = snapshot;
            return;
        }

        snapshot.isValid_ = true;
        snapshot.lifecycleState_ = data.lifecycleState;

        snapshot.registeredSessionCount_ = data.registeredSessionCount;
        snapshot.closingSessionCount_ = data.closingSessionCount;
        snapshot.pendingRecvIoCount_ = data.pendingRecvIoCount;
        snapshot.pendingSendIoCount_ = data.pendingSendIoCount;
        snapshot.sendMailboxDepth_ = data.sendMailboxDepth;
        snapshot.sendMailboxHighWatermark_ = data.sendMailboxHighWatermark;
        snapshot.pendingSendQueueDepth_ = data.pendingSendQueueDepth;
        snapshot.pendingSendQueueHighWatermark_ = data.pendingSendQueueHighWatermark;

        snapshot.toWorldEventDepth_ = data.toWorldEventDepth;
        snapshot.toWorldEventHighWatermark_ = data.toWorldEventHighWatermark;

        std::uint64_t totalPressureTransactions = 0;
        for (std::size_t index = 0; index < NrPressureTransactionOutcomeCount; ++index)
        {
            snapshot.pressureTransactionCounts_[index] = data.pressureTransactionCounts[index];
            totalPressureTransactions = SaturatingAdd(totalPressureTransactions, data.pressureTransactionCounts[index]);
        }

        for (std::size_t roleIndex = 0; roleIndex < NrServerMemoryPoolRoleCount; ++roleIndex)
        {
            snapshot.memoryPools_[roleIndex] = data.memoryPools[roleIndex];

            for (std::size_t outcomeIndex = 0; outcomeIndex < NrPoolPressureOutcomeCount; ++outcomeIndex)
            {
                const std::uint64_t count = data.poolAcquirePressureCounts[roleIndex][outcomeIndex];
                snapshot.poolAcquirePressureCounts_[roleIndex][outcomeIndex] = count;
                totalPressureTransactions = SaturatingAdd(totalPressureTransactions, count);
            }
        }

        snapshot.totalPressureTransactions_ = totalPressureTransactions;
        snapshot.diagnostics_ = data.diagnostics;
        outSnapshot = snapshot;
    }
} // namespace psnr::runtime
