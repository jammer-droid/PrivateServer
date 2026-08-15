#include "pch.h"

#include "NrClientLifecycleStateMachine.h"

#include "NrErrorCode.h"

#include <limits>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    NrClientLifecycleState NrClientLifecycleStateMachine::State() const noexcept
    {
        return state_;
    }

    std::uint64_t NrClientLifecycleStateMachine::CurrentGeneration() const noexcept
    {
        return currentGeneration_;
    }

    NrStatus NrClientLifecycleStateMachine::BeginConnect(std::uint64_t& outGeneration) noexcept
    {
        // !TODO: 방어용 if 문 assert로 정리하기(논리적으로 발생하면 안되는 코드들)
        if (state_ != NrClientLifecycleState::Idle)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (currentGeneration_ == std::numeric_limits<std::uint64_t>::max()) // !TODO: 발생하지 않을 예외 정리
        {
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        ResetAttemptPublication();
        ++currentGeneration_;
        state_ = NrClientLifecycleState::TransportConnecting;
        outGeneration = currentGeneration_;
        return NrStatus::Success();
    }

    NrStatus NrClientLifecycleStateMachine::RecordConnectSucceeded(const std::uint64_t generation) noexcept
    {
        if (generation != currentGeneration_)
        {
            return NrStatus::Success();
        }

        if (state_ == NrClientLifecycleState::TransportDisconnecting &&
            attemptOutcomeKind_ == NrClientLifecycleNotificationKind::None)
        {
            // BeginConnect -> ConnectEx 요청 후 Completion 도착 전에 RequestDisconnect 호출된 경우
            // Disconnect 가 Accept 된 상황이므로, Disconnect 의도를 우선시하여 Success 반환
            return NrStatus::Success();
        }

        if (state_ != NrClientLifecycleState::TransportConnecting ||
            attemptOutcomeKind_ != NrClientLifecycleNotificationKind::None || disconnectedRecorded_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        attemptOutcomeKind_ = NrClientLifecycleNotificationKind::TransportConnected;
        state_ = NrClientLifecycleState::TransportConnected;
        return NrStatus::Success();
    }

    NrStatus NrClientLifecycleStateMachine::RecordConnectFailed(const std::uint64_t generation,
                                                                const NrStatus transportStatus) noexcept
    {
        if (transportStatus.Succeeded())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (generation != currentGeneration_)
        {
            return NrStatus::Success();
        }

        if (state_ == NrClientLifecycleState::TransportDisconnecting &&
            attemptOutcomeKind_ == NrClientLifecycleNotificationKind::None)
        {
            return NrStatus::Success();
        }

        if (state_ != NrClientLifecycleState::TransportConnecting ||
            attemptOutcomeKind_ != NrClientLifecycleNotificationKind::None || disconnectedRecorded_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        attemptOutcomeKind_ = NrClientLifecycleNotificationKind::TransportConnectionFailed;
        attemptOutcomeStatus_ = transportStatus;
        return NrStatus::Success();
    }

    NrStatus NrClientLifecycleStateMachine::RequestDisconnect() noexcept
    {
        switch (state_)
        {
        case NrClientLifecycleState::TransportConnecting:
        case NrClientLifecycleState::TransportConnected:
            state_ = NrClientLifecycleState::TransportDisconnecting;
            return NrStatus::Success();

        case NrClientLifecycleState::TransportDisconnecting:
            return NrStatus::Success();

        case NrClientLifecycleState::Invalid:
        case NrClientLifecycleState::Idle:
        case NrClientLifecycleState::Shutdown:
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    NrStatus NrClientLifecycleStateMachine::RecordDisconnected(const std::uint64_t generation,
                                                               const NrClientDisconnectReason reason,
                                                               const NrStatus transportStatus) noexcept
    {
        if (reason == NrClientDisconnectReason::None)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (generation != currentGeneration_)
        {
            return NrStatus::Success();
        }

        if ((state_ != NrClientLifecycleState::TransportConnected &&
             state_ != NrClientLifecycleState::TransportDisconnecting) ||
            attemptOutcomeKind_ == NrClientLifecycleNotificationKind::TransportConnectionFailed ||
            disconnectedRecorded_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        disconnectedStatus_ = transportStatus;
        disconnectReason_ = reason;
        disconnectedRecorded_ = true;
        state_ = NrClientLifecycleState::TransportDisconnecting;
        return NrStatus::Success();
    }

    NrStatus NrClientLifecycleStateMachine::Shutdown() noexcept
    {
        if (state_ == NrClientLifecycleState::Shutdown)
        {
            return NrStatus::Success();
        }

        ResetAttemptPublication();
        state_ = NrClientLifecycleState::Shutdown;
        return NrStatus::Success();
    }

    // StateMachine 에 기록됐지만, 아직 NrClientEvent 저장소로 넘기지 않은 lifecycle 결과 중, 다음으로 처리할 종류 반환
    // 다음 순서로 처리
    // 1. TransportConnected or TransportConnectionFailed
    // 2. TransportDisconnected
    // 3. 없으면 None
    NrClientLifecycleNotificationKind NrClientLifecycleStateMachine::NextPendingKind() const noexcept
    {
        if (attemptOutcomeKind_ != NrClientLifecycleNotificationKind::None && !attemptOutcomeCommitted_)
        {
            return attemptOutcomeKind_;
        }

        if (disconnectedRecorded_ && !disconnectedCommitted_ &&
            (attemptOutcomeKind_ == NrClientLifecycleNotificationKind::None || attemptOutcomeCommitted_))
        {
            return NrClientLifecycleNotificationKind::TransportDisconnected;
        }

        return NrClientLifecycleNotificationKind::None;
    }

    NrStatus NrClientLifecycleStateMachine::GetPendingTransportStatus(NrStatus& outStatus) const noexcept
    {
        switch (NextPendingKind())
        {
        case NrClientLifecycleNotificationKind::TransportConnectionFailed:
            outStatus = attemptOutcomeStatus_;
            return NrStatus::Success();

        case NrClientLifecycleNotificationKind::TransportDisconnected:
            outStatus = disconnectedStatus_;
            return NrStatus::Success();

        case NrClientLifecycleNotificationKind::None:
        case NrClientLifecycleNotificationKind::TransportConnected:
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    // !TODO: out 파라미터는 * 를 사용해서 호출자 입장에서 &를 명시적으로 작성하게
    // !TODO: in 파라미터는 가능하면 & 를 사용해서 null check 가 없을 수 있도록(+ 본인의 소유권이 아닌 composition 이라는 것을 명시)
    NrStatus NrClientLifecycleStateMachine::GetPendingDisconnectReason(
        NrClientDisconnectReason& outReason) const noexcept
    {
        if (NextPendingKind() != NrClientLifecycleNotificationKind::TransportDisconnected)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        outReason = disconnectReason_;
        return NrStatus::Success();
    }

    NrStatus NrClientLifecycleStateMachine::CommitNextPending() noexcept
    {
        switch (NextPendingKind())
        {
        case NrClientLifecycleNotificationKind::TransportConnected:
            attemptOutcomeCommitted_ = true;
            return NrStatus::Success();

        case NrClientLifecycleNotificationKind::TransportConnectionFailed:
            attemptOutcomeCommitted_ = true;
            state_ = NrClientLifecycleState::Idle;
            return NrStatus::Success();

        case NrClientLifecycleNotificationKind::TransportDisconnected:
            disconnectedCommitted_ = true;
            state_ = NrClientLifecycleState::Idle;
            return NrStatus::Success();

        case NrClientLifecycleNotificationKind::None:
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    bool NrClientLifecycleStateMachine::CanSend() const noexcept
    {
        return state_ == NrClientLifecycleState::TransportConnected &&
               attemptOutcomeKind_ == NrClientLifecycleNotificationKind::TransportConnected &&
               attemptOutcomeCommitted_ && !disconnectedRecorded_;
    }

    bool NrClientLifecycleStateMachine::CanPublishPacket() const noexcept
    {
        return CanSend();
    }

    void NrClientLifecycleStateMachine::ResetAttemptPublication() noexcept
    {
        attemptOutcomeKind_ = NrClientLifecycleNotificationKind::None;
        attemptOutcomeStatus_ = NrStatus::Success();
        attemptOutcomeCommitted_ = false;
        disconnectedStatus_ = NrStatus::Success();
        disconnectReason_ = NrClientDisconnectReason::None;
        disconnectedRecorded_ = false;
        disconnectedCommitted_ = false;
    }
} // namespace psnr::runtime::internal
