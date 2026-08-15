#include "pch.h"

#include "NrToWorldLifecycleState.h"

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;

    namespace
    {
        TEST(NrToWorldLifecycleStateTests, DefaultStateHasNoPublicationAndRejectsPackets)
        {
            const NrToWorldLifecycleState state;

            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::None);
            EXPECT_FALSE(state.CanPublishPacket());
            EXPECT_FALSE(state.IsSessionClosedPublished());
        }

        TEST(NrToWorldLifecycleStateTests, AcceptedMustCommitBeforePacketsCanPublish)
        {
            NrToWorldLifecycleState state;

            ASSERT_TRUE(state.RecordAccepted().Succeeded());
            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::SessionAccepted);
            EXPECT_FALSE(state.CanPublishPacket());

            ASSERT_TRUE(state.CommitNextPending().Succeeded());
            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::None);
            EXPECT_TRUE(state.CanPublishPacket());
        }

        TEST(NrToWorldLifecycleStateTests, ClosedImmediatelyBlocksPacketsAndPreservesEndReason)
        {
            NrToWorldLifecycleState state;
            ASSERT_TRUE(state.RecordAccepted().Succeeded());
            ASSERT_TRUE(state.CommitNextPending().Succeeded());

            ASSERT_TRUE(state.RecordClosed(NrSessionEndReason::RemoteClosed).Succeeded());
            EXPECT_FALSE(state.CanPublishPacket());
            EXPECT_FALSE(state.IsSessionClosedPublished());
            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::SessionClosed);

            NrSessionEndReason reason = NrSessionEndReason::None;
            ASSERT_TRUE(state.GetEndReason(reason).Succeeded());
            EXPECT_EQ(reason, NrSessionEndReason::RemoteClosed);

            ASSERT_TRUE(state.CommitNextPending().Succeeded());
            EXPECT_TRUE(state.IsSessionClosedPublished());
            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::None);
        }

        TEST(NrToWorldLifecycleStateTests, CloseBeforeAcceptedCommitStillPublishesAcceptedFirst)
        {
            NrToWorldLifecycleState state;
            ASSERT_TRUE(state.RecordAccepted().Succeeded());
            ASSERT_TRUE(state.RecordClosed(NrSessionEndReason::TransportError).Succeeded());

            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::SessionAccepted);
            EXPECT_FALSE(state.CanPublishPacket());

            ASSERT_TRUE(state.CommitNextPending().Succeeded());
            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::SessionClosed);

            ASSERT_TRUE(state.CommitNextPending().Succeeded());
            EXPECT_TRUE(state.IsSessionClosedPublished());
        }

        TEST(NrToWorldLifecycleStateTests, InvalidTransitionsDoNotChangeStateOrOutputs)
        {
            NrToWorldLifecycleState state;
            NrSessionEndReason reason = NrSessionEndReason::SendPressure;

            EXPECT_EQ(state.CommitNextPending().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(state.RecordClosed(NrSessionEndReason::RemoteClosed).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(state.GetEndReason(reason).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(reason, NrSessionEndReason::SendPressure);

            ASSERT_TRUE(state.RecordAccepted().Succeeded());
            EXPECT_EQ(state.RecordAccepted().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_EQ(state.RecordClosed(NrSessionEndReason::None).ErrorCode(), NrErrorCode::InvalidArgument);
            EXPECT_EQ(state.NextPendingKind(), NrToWorldLifecycleNotificationKind::SessionAccepted);

            ASSERT_TRUE(state.RecordClosed(NrSessionEndReason::ApplicationPolicy).Succeeded());
            EXPECT_EQ(state.RecordClosed(NrSessionEndReason::TransportError).ErrorCode(), NrErrorCode::InvalidState);

            ASSERT_TRUE(state.CommitNextPending().Succeeded());
            ASSERT_TRUE(state.CommitNextPending().Succeeded());
            EXPECT_EQ(state.CommitNextPending().ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_TRUE(state.IsSessionClosedPublished());

            reason = NrSessionEndReason::None;
            ASSERT_TRUE(state.GetEndReason(reason).Succeeded());
            EXPECT_EQ(reason, NrSessionEndReason::ApplicationPolicy);
        }
    } // namespace
} // namespace psnr::runtime::internal
