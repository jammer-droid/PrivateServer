#pragma once

#include "NrClientEvent.h"
#include "NrClientSnapshot.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    enum class NrClientLifecycleNotificationKind : std::uint8_t
    {
        None,
        TransportConnected,
        TransportConnectionFailed,
        TransportDisconnected,
    };

    class NrClientLifecycleStateMachine final
    {
    public:
        [[nodiscard]] NrClientLifecycleState State() const noexcept;
        [[nodiscard]] std::uint64_t CurrentGeneration() const noexcept;

        [[nodiscard]] psnr::core::NrStatus BeginConnect(std::uint64_t& outGeneration) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordConnectSucceeded(std::uint64_t generation) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordConnectFailed(std::uint64_t generation,
                                                               psnr::core::NrStatus transportStatus) noexcept;
        [[nodiscard]] psnr::core::NrStatus RequestDisconnect() noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordDisconnected(std::uint64_t generation, NrClientDisconnectReason reason,
                                                              psnr::core::NrStatus transportStatus) noexcept;
        [[nodiscard]] psnr::core::NrStatus Shutdown() noexcept;

        [[nodiscard]] NrClientLifecycleNotificationKind NextPendingKind() const noexcept;
        [[nodiscard]] psnr::core::NrStatus GetPendingTransportStatus(psnr::core::NrStatus& outStatus) const noexcept;
        [[nodiscard]] psnr::core::NrStatus GetPendingDisconnectReason(
            NrClientDisconnectReason& outReason) const noexcept;
        [[nodiscard]] psnr::core::NrStatus CommitNextPending() noexcept;

        [[nodiscard]] bool CanSend() const noexcept;
        [[nodiscard]] bool CanPublishPacket() const noexcept;

    private:
        void ResetAttemptPublication() noexcept;

        NrClientLifecycleState state_ = NrClientLifecycleState::Idle;
        std::uint64_t currentGeneration_ = 0;

        // AttemptOutcome: 한 번의 연결 시도 결과
        // 연결 시도 = BeginConnect() -> Connection Generation 시작 -> Connected / ConnectionFailed
        NrClientLifecycleNotificationKind attemptOutcomeKind_ = NrClientLifecycleNotificationKind::None;
        psnr::core::NrStatus attemptOutcomeStatus_;
        bool attemptOutcomeCommitted_ = false; // public event storage 에 enqueue 성공 했다는 표시

        psnr::core::NrStatus disconnectedStatus_;
        NrClientDisconnectReason disconnectReason_ = NrClientDisconnectReason::None;
        bool disconnectedRecorded_ = false;
        bool disconnectedCommitted_ = false;
    };
} // namespace psnr::runtime::internal
