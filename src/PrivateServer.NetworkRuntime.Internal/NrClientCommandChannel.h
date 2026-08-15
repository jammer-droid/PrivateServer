#pragma once

#include "NrActorScheduleGate.h"
#include "NrClientCommand.h"
#include "NrClientPendingSend.h"
#include "NrClientSnapshot.h"
#include "NrStatus.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::runtime
{
    class NrIocpPort;
}

namespace psnr::core
{
    class NrMemoryPoolManager;
    struct NrPacketType;
} // namespace psnr::core

namespace psnr::runtime::internal
{
    class NrClientCommandSlot final
    {
    public:
        NrClientCommandSlot() noexcept = default;

        NrClientCommandSlot(const NrClientCommandSlot&) = delete;
        NrClientCommandSlot& operator=(const NrClientCommandSlot&) = delete;

        NrClientCommandSlot(NrClientCommandSlot&&) = delete;
        NrClientCommandSlot& operator=(NrClientCommandSlot&&) = delete;

        ~NrClientCommandSlot() noexcept = default;

        [[nodiscard]] bool TryPublish(const NrClientCommand& command) noexcept;
        [[nodiscard]] bool TryConsume(NrClientCommand& outCommand) noexcept;

    private:
        enum class State : std::uint8_t // command slot의 소유권 및 데이터 완성 상태
        {
            Empty,
            Writing,
            Ready,
            Reading,
        };

        static_assert(std::atomic<State>::is_always_lock_free,
                      "NrClientCommandSlot requires a lock-free atomic publication state.");

        std::atomic<State> state_{State::Empty};
        NrClientCommand command_;
    };

    class NrClientCommandChannel final
    {
    public:
        NrClientCommandChannel(NrIocpPort& iocpPort, psnr::core::NrMemoryPoolManager& memoryPoolManager,
                               NrClientPendingSendQueue& pendingSendQueue) noexcept;

        NrClientCommandChannel(const NrClientCommandChannel&) = delete;
        NrClientCommandChannel& operator=(const NrClientCommandChannel&) = delete;

        NrClientCommandChannel(NrClientCommandChannel&&) = delete;
        NrClientCommandChannel& operator=(NrClientCommandChannel&&) = delete;

        ~NrClientCommandChannel() noexcept;

        [[nodiscard]] psnr::core::NrStatus SubmitConnect(const NrEndpoint& remoteEndpoint,
                                                         std::uint64_t& outAttemptGeneration) noexcept;
        [[nodiscard]] psnr::core::NrStatus SubmitSend(psnr::core::NrPacketType packetType,
                                                      std::span<const std::byte> semanticPayload) noexcept;
        [[nodiscard]] psnr::core::NrStatus SubmitDisconnect() noexcept;
        [[nodiscard]] psnr::core::NrStatus SubmitShutdown() noexcept;
        [[nodiscard]] psnr::core::NrStatus SignalEventSpaceAvailable() noexcept;

        [[nodiscard]] psnr::core::NrStatus RecordConnectSucceeded(std::uint64_t attemptGeneration) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordConnectFailed(std::uint64_t attemptGeneration) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordDisconnected(std::uint64_t attemptGeneration) noexcept;

        [[nodiscard]] bool TryBeginDrain() noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteDrain(bool shouldReschedule) noexcept;
        [[nodiscard]] psnr::core::NrStatus TryPopCommand(NrClientCommand& outCommand) noexcept;
        [[nodiscard]] psnr::core::NrStatus TryPopSend(NrClientPendingSend& outSend) noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteAcceptedSend() noexcept;
        [[nodiscard]] psnr::core::NrStatus DiscardPendingSends() noexcept;

        [[nodiscard]] NrClientLifecycleState State() const noexcept;
        [[nodiscard]] std::uint64_t CurrentGeneration() const noexcept;
        [[nodiscard]] psnr::core::NrActorScheduleView ScheduleView() const noexcept;
        [[nodiscard]] bool HasWakeFailure() const noexcept;
        [[nodiscard]] std::size_t SendPipelineDepth() const noexcept;
        [[nodiscard]] std::size_t PendingSendQueueDepth() const noexcept;
        [[nodiscard]] std::size_t PendingSendQueueHighWatermark() const noexcept;

    private:
        static_assert(std::atomic<NrClientLifecycleState>::is_always_lock_free,
                      "NrClientCommandChannel requires a lock-free lifecycle projection.");
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                      "NrClientCommandChannel requires lock-free generation counters.");
        static_assert(std::atomic<std::size_t>::is_always_lock_free,
                      "NrClientCommandChannel requires lock-free depth counters.");

        [[nodiscard]] psnr::core::NrStatus CompleteAdmission(
            psnr::core::NrActorAdmissionTicket&& ticket, psnr::core::NrActorAdmissionResolution resolution) noexcept;
        [[nodiscard]] psnr::core::NrStatus ApplyScheduleDirective(
            psnr::core::NrActorScheduleDirective directive) noexcept;
        void RecordWakeFailure() noexcept;
        [[nodiscard]] bool TryReserveSendPipelineSlot() noexcept;
        [[nodiscard]] bool TryReleaseSendPipelineSlot() noexcept;
        void UpdatePendingSendQueueHighWatermark(std::size_t depth) noexcept;

        NrIocpPort& iocpPort_;
        psnr::core::NrMemoryPoolManager& memoryPoolManager_;
        NrClientPendingSendQueue& pendingSendQueue_;

        psnr::core::NrActorScheduleGate scheduleGate_;
        std::atomic<NrClientLifecycleState> state_{NrClientLifecycleState::Idle};
        std::atomic<std::uint64_t> currentGeneration_{0};
        NrClientCommandSlot pendingConnect_;
        NrClientCommandSlot pendingDisconnect_;
        NrClientCommandSlot pendingShutdown_;
        NrClientCommandSlot pendingEventSpaceAvailable_;

        std::atomic_bool wakeFailure_{false};
        std::atomic<std::uint64_t> sendAdmissionGeneration_{0};
        std::atomic<std::size_t> sendPipelineDepth_{0};
        std::atomic<std::size_t> pendingSendQueueDepth_{0};
        std::atomic<std::size_t> pendingSendQueueHighWatermark_{0};
    };
} // namespace psnr::runtime::internal
