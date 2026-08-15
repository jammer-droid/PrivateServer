#include "pch.h"

#include "NrClientLifecycleStateMachine.h"

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    namespace
    {
        TEST(NrClientLifecycleStateMachineTests, DefaultStateMachineIsIdleWithoutPendingPublication)
        {
            const NrClientLifecycleStateMachine stateMachine;

            EXPECT_EQ(stateMachine.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(stateMachine.CurrentGeneration(), 0u);
            EXPECT_EQ(stateMachine.NextPendingKind(), NrClientLifecycleNotificationKind::None);
            EXPECT_FALSE(stateMachine.CanSend());
            EXPECT_FALSE(stateMachine.CanPublishPacket());
        }

        TEST(NrClientLifecycleStateMachineTests, ConnectedMustCommitBeforeSendOrPacketPublication)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t generation = 0;
            ASSERT_TRUE(core.BeginConnect(generation).Succeeded());
            ASSERT_EQ(generation, 1u);

            ASSERT_TRUE(core.RecordConnectSucceeded(generation).Succeeded());
            EXPECT_EQ(core.State(), NrClientLifecycleState::TransportConnected);
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::TransportConnected);
            EXPECT_FALSE(core.CanSend());
            EXPECT_FALSE(core.CanPublishPacket());

            ASSERT_TRUE(core.CommitNextPending().Succeeded());
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::None);
            EXPECT_TRUE(core.CanSend());
            EXPECT_TRUE(core.CanPublishPacket());
        }

        TEST(NrClientLifecycleStateMachineTests, ConnectionFailurePreservesStatusAndReturnsIdleAfterCommit)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t generation = 0;
            ASSERT_TRUE(core.BeginConnect(generation).Succeeded());

            const NrStatus failure = NrStatus::Failure(NrErrorCode::IoFailed, 10061);
            ASSERT_TRUE(core.RecordConnectFailed(generation, failure).Succeeded());
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::TransportConnectionFailed);

            NrStatus recordedStatus = NrStatus::Success();
            ASSERT_TRUE(core.GetPendingTransportStatus(recordedStatus).Succeeded());
            EXPECT_EQ(recordedStatus.ErrorCode(), NrErrorCode::IoFailed);
            EXPECT_EQ(recordedStatus.NativeErrorCode(), 10061u);

            ASSERT_TRUE(core.CommitNextPending().Succeeded());
            EXPECT_EQ(core.State(), NrClientLifecycleState::Idle);
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::None);
        }

        TEST(NrClientLifecycleStateMachineTests, ConnectedOutcomePublishesBeforeDisconnected)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t generation = 0;
            ASSERT_TRUE(core.BeginConnect(generation).Succeeded());
            ASSERT_TRUE(core.RecordConnectSucceeded(generation).Succeeded());
            ASSERT_TRUE(core.RequestDisconnect().Succeeded());
            ASSERT_TRUE(
                core.RecordDisconnected(generation, NrClientDisconnectReason::LocalRequested, NrStatus::Success())
                    .Succeeded());

            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::TransportConnected);
            EXPECT_FALSE(core.CanSend());
            ASSERT_TRUE(core.CommitNextPending().Succeeded());

            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::TransportDisconnected);
            NrClientDisconnectReason reason = NrClientDisconnectReason::None;
            ASSERT_TRUE(core.GetPendingDisconnectReason(reason).Succeeded());
            EXPECT_EQ(reason, NrClientDisconnectReason::LocalRequested);

            ASSERT_TRUE(core.CommitNextPending().Succeeded());
            EXPECT_EQ(core.State(), NrClientLifecycleState::Idle);
        }

        TEST(NrClientLifecycleStateMachineTests, DisconnectWhileConnectingNeedsOnlyTerminalNotification)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t generation = 0;
            ASSERT_TRUE(core.BeginConnect(generation).Succeeded());
            ASSERT_TRUE(core.RequestDisconnect().Succeeded());

            EXPECT_TRUE(core.RecordConnectSucceeded(generation).Succeeded());
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::None);

            ASSERT_TRUE(
                core.RecordDisconnected(generation, NrClientDisconnectReason::LocalRequested, NrStatus::Success())
                    .Succeeded());
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::TransportDisconnected);
        }

        TEST(NrClientLifecycleStateMachineTests, StaleCompletionsDoNotChangeCurrentAttempt)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t firstGeneration = 0;
            ASSERT_TRUE(core.BeginConnect(firstGeneration).Succeeded());
            ASSERT_TRUE(core.RequestDisconnect().Succeeded());
            ASSERT_TRUE(
                core.RecordDisconnected(firstGeneration, NrClientDisconnectReason::LocalRequested, NrStatus::Success())
                    .Succeeded());
            ASSERT_TRUE(core.CommitNextPending().Succeeded());

            std::uint64_t secondGeneration = 0;
            ASSERT_TRUE(core.BeginConnect(secondGeneration).Succeeded());
            ASSERT_GT(secondGeneration, firstGeneration);

            EXPECT_TRUE(core.RecordConnectSucceeded(firstGeneration).Succeeded());
            EXPECT_TRUE(
                core.RecordConnectFailed(firstGeneration, NrStatus::Failure(NrErrorCode::IoFailed, 1)).Succeeded());
            EXPECT_TRUE(
                core.RecordDisconnected(firstGeneration, NrClientDisconnectReason::RemoteClosed, NrStatus::Success())
                    .Succeeded());

            EXPECT_EQ(core.State(), NrClientLifecycleState::TransportConnecting);
            EXPECT_EQ(core.CurrentGeneration(), secondGeneration);
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::None);
        }

        TEST(NrClientLifecycleStateMachineTests, InvalidTransitionsPreserveOutputsAndPendingState)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t generation = 77;
            EXPECT_EQ(core.RequestDisconnect().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(core.CommitNextPending().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(core.BeginConnect(generation).ErrorCode(), NrErrorCode::Success);
            EXPECT_EQ(generation, 1u);

            std::uint64_t rejectedGeneration = 91;
            EXPECT_EQ(core.BeginConnect(rejectedGeneration).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(rejectedGeneration, 91u);
            EXPECT_EQ(core.RecordConnectFailed(generation, NrStatus::Success()).ErrorCode(),
                      NrErrorCode::InvalidArgument);
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::None);

            NrStatus pendingStatus = NrStatus::Failure(NrErrorCode::ProtocolError, 7);
            NrClientDisconnectReason reason = NrClientDisconnectReason::ProtocolError;
            EXPECT_EQ(core.GetPendingTransportStatus(pendingStatus).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(core.GetPendingDisconnectReason(reason).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(pendingStatus.ErrorCode(), NrErrorCode::ProtocolError);
            EXPECT_EQ(pendingStatus.NativeErrorCode(), 7u);
            EXPECT_EQ(reason, NrClientDisconnectReason::ProtocolError);
        }

        TEST(NrClientLifecycleStateMachineTests, DisconnectAndShutdownCleanupAreIdempotent)
        {
            NrClientLifecycleStateMachine core;
            std::uint64_t generation = 0;
            ASSERT_TRUE(core.BeginConnect(generation).Succeeded());
            ASSERT_TRUE(core.RequestDisconnect().Succeeded());
            EXPECT_TRUE(core.RequestDisconnect().Succeeded());

            ASSERT_TRUE(core.Shutdown().Succeeded());
            EXPECT_TRUE(core.Shutdown().Succeeded());
            EXPECT_EQ(core.State(), NrClientLifecycleState::Shutdown);
            EXPECT_EQ(core.NextPendingKind(), NrClientLifecycleNotificationKind::None);
            EXPECT_EQ(core.BeginConnect(generation).ErrorCode(), NrErrorCode::InvalidState);
        }
    } // namespace
} // namespace psnr::runtime::internal
