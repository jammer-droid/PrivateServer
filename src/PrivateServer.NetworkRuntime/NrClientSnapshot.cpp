#include "pch.h"

#include "NrClientSnapshot.h"

#include "NrClientSnapshotAccess.h"

#include <limits>

namespace psnr::runtime
{
    namespace
    {
        // !TODO: 공용 helper 함수로 추출하기
        [[nodiscard]] constexpr std::uint64_t SaturatingAdd(const std::uint64_t left,
                                                            const std::uint64_t right) noexcept
        {
            constexpr std::uint64_t MaxValue = std::numeric_limits<std::uint64_t>::max();
            return right > MaxValue - left ? MaxValue : left + right;
        }
    } // namespace

    NrClientSnapshot::NrClientSnapshot() noexcept = default;

    NrClientSnapshot::~NrClientSnapshot() noexcept = default;

    bool NrClientSnapshot::IsValid() const noexcept
    {
        return isValid_;
    }

    NrClientLifecycleState NrClientSnapshot::LifecycleState() const noexcept
    {
        return lifecycleState_;
    }

    std::uint64_t NrClientSnapshot::PendingConnectIoCount() const noexcept
    {
        return pendingConnectIoCount_;
    }

    std::uint64_t NrClientSnapshot::PendingRecvIoCount() const noexcept
    {
        return pendingRecvIoCount_;
    }

    std::uint64_t NrClientSnapshot::PendingSendIoCount() const noexcept
    {
        return pendingSendIoCount_;
    }

    std::uint64_t NrClientSnapshot::PendingIoCount() const noexcept
    {
        return SaturatingAdd(SaturatingAdd(pendingConnectIoCount_, pendingRecvIoCount_), pendingSendIoCount_);
    }

    std::uint64_t NrClientSnapshot::EventQueueDepth() const noexcept
    {
        return eventQueueDepth_;
    }

    std::uint64_t NrClientSnapshot::EventQueueHighWatermark() const noexcept
    {
        return eventQueueHighWatermark_;
    }

    std::uint64_t NrClientSnapshot::PendingSendQueueDepth() const noexcept
    {
        return pendingSendQueueDepth_;
    }

    std::uint64_t NrClientSnapshot::PendingSendQueueHighWatermark() const noexcept
    {
        return pendingSendQueueHighWatermark_;
    }

    void internal::NrClientSnapshotAccess::Assign(const NrClientSnapshotData& data,
                                                  NrClientSnapshot& outSnapshot) noexcept
    {
        NrClientSnapshot snapshot;
        snapshot.isValid_ = true;
        snapshot.lifecycleState_ = data.lifecycleState;
        snapshot.pendingConnectIoCount_ = data.pendingConnectIoCount;
        snapshot.pendingRecvIoCount_ = data.pendingRecvIoCount;
        snapshot.pendingSendIoCount_ = data.pendingSendIoCount;
        snapshot.eventQueueDepth_ = data.eventQueueDepth;
        snapshot.eventQueueHighWatermark_ = data.eventQueueHighWatermark;
        snapshot.pendingSendQueueDepth_ = data.pendingSendQueueDepth;
        snapshot.pendingSendQueueHighWatermark_ = data.pendingSendQueueHighWatermark;
        outSnapshot = snapshot;
    }
} // namespace psnr::runtime
