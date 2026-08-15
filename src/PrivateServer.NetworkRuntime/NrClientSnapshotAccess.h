#pragma once

#include "NrClientSnapshot.h"

namespace psnr::runtime::internal
{
    struct NrClientSnapshotData final
    {
        NrClientLifecycleState lifecycleState = NrClientLifecycleState::Invalid;

        std::uint64_t pendingConnectIoCount = 0;
        std::uint64_t pendingRecvIoCount = 0;
        std::uint64_t pendingSendIoCount = 0;

        std::uint64_t eventQueueDepth = 0;
        std::uint64_t eventQueueHighWatermark = 0;
        std::uint64_t pendingSendQueueDepth = 0;
        std::uint64_t pendingSendQueueHighWatermark = 0;
    };

    class NrClientSnapshotAccess final
    {
    public:
        static void Assign(const NrClientSnapshotData& data, NrClientSnapshot& outSnapshot) noexcept;
    };
} // namespace psnr::runtime::internal
