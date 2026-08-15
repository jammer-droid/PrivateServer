#pragma once

#include "Export.h"

#include <cstdint>

namespace psnr::runtime
{
    namespace internal
    {
        class NrClientSnapshotAccess;
    } // namespace internal

    enum class NrClientLifecycleState : std::uint8_t
    {
        Invalid,
        Idle,
        TransportConnecting,
        TransportConnected,
        TransportDisconnecting,
        Shutdown,
    };

    class NrClientSnapshot final
    {
    public:
        PSNR_API NrClientSnapshot() noexcept;

        NrClientSnapshot(const NrClientSnapshot&) noexcept = default;
        NrClientSnapshot& operator=(const NrClientSnapshot&) noexcept = default;

        NrClientSnapshot(NrClientSnapshot&&) noexcept = default;
        NrClientSnapshot& operator=(NrClientSnapshot&&) noexcept = default;

        PSNR_API ~NrClientSnapshot() noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API NrClientLifecycleState LifecycleState() const noexcept;

        [[nodiscard]] PSNR_API std::uint64_t PendingConnectIoCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingRecvIoCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingSendIoCount() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingIoCount() const noexcept;

        [[nodiscard]] PSNR_API std::uint64_t EventQueueDepth() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t EventQueueHighWatermark() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingSendQueueDepth() const noexcept;
        [[nodiscard]] PSNR_API std::uint64_t PendingSendQueueHighWatermark() const noexcept;

    private:
        friend class internal::NrClientSnapshotAccess;

        bool isValid_ = false;
        NrClientLifecycleState lifecycleState_ = NrClientLifecycleState::Invalid;

        std::uint64_t pendingConnectIoCount_ = 0;
        std::uint64_t pendingRecvIoCount_ = 0;
        std::uint64_t pendingSendIoCount_ = 0;

        std::uint64_t eventQueueDepth_ = 0;
        std::uint64_t eventQueueHighWatermark_ = 0;
        std::uint64_t pendingSendQueueDepth_ = 0;
        std::uint64_t pendingSendQueueHighWatermark_ = 0;
    };
} // namespace psnr::runtime
