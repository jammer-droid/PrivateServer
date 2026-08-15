#pragma once

#include "NrServerSnapshot.h"

namespace psnr::runtime::internal
{
    struct NrServerSnapshotData final
    {
        NrServerLifecycleState lifecycleState = NrServerLifecycleState::Invalid;

        std::uint64_t registeredSessionCount = 0;
        std::uint64_t closingSessionCount = 0;
        std::uint64_t pendingRecvIoCount = 0;
        std::uint64_t pendingSendIoCount = 0;
        std::uint64_t sendMailboxDepth = 0;
        std::uint64_t sendMailboxHighWatermark = 0;
        std::uint64_t pendingSendQueueDepth = 0;
        std::uint64_t pendingSendQueueHighWatermark = 0;

        std::uint64_t toWorldEventDepth = 0;
        std::uint64_t toWorldEventHighWatermark = 0;

        std::uint64_t pressureTransactionCounts[NrPressureTransactionOutcomeCount]{};
        std::uint64_t poolAcquirePressureCounts[NrServerMemoryPoolRoleCount][NrPoolPressureOutcomeCount]{};
        NrServerMemoryPoolSnapshot memoryPools[NrServerMemoryPoolRoleCount]{};
        NrServerDiagnosticsSnapshot diagnostics{};
    };

    class NrServerSnapshotAccess final
    {
    public:
        static void Assign(const NrServerSnapshotData& data, NrServerSnapshot& outSnapshot) noexcept;
    };
} // namespace psnr::runtime::internal
