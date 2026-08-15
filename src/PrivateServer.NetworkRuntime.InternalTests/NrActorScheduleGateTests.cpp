#include "pch.h"

#include "NrActorScheduleGate.h"

#include <utility>

namespace psnr::core
{
    namespace
    {
        void ScheduleActor(NrActorScheduleGate& gate)
        {
            NrActorAdmissionTicket ticket = gate.TryBeginAdmission();
            ASSERT_TRUE(ticket.Accepted());
            ASSERT_TRUE(ticket.NeedsNewPermit());
            ASSERT_EQ(gate.CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted),
                      NrActorScheduleDirective::EnqueueReadyToken);
        }

        TEST(NrActorScheduleGateTests, StartsIdle)
        {
            NrActorScheduleGate gate;

            EXPECT_EQ(gate.View(), NrActorScheduleView::Idle);
        }

        TEST(NrActorScheduleGateTests, CapacityRejectionAbortsNewAdmissionWithoutPermitRelease)
        {
            NrActorScheduleGate gate;
            NrActorAdmissionTicket ticket = gate.TryBeginAdmission();

            ASSERT_TRUE(ticket.Accepted());
            ASSERT_TRUE(ticket.NeedsNewPermit());
            EXPECT_EQ(gate.View(), NrActorScheduleView::Admitting);

            const NrActorScheduleDirective directive =
                gate.CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::CapacityRejected);

            EXPECT_EQ(directive, NrActorScheduleDirective::NoAction);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Idle);
        }

        TEST(NrActorScheduleGateTests, MailboxRejectionAfterPermitReservationReleasesPermit)
        {
            NrActorScheduleGate gate;
            NrActorAdmissionTicket ticket = gate.TryBeginAdmission();

            const NrActorScheduleDirective directive =
                gate.CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxRejected);

            EXPECT_EQ(directive, NrActorScheduleDirective::ReleasePermit);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Idle);
        }

        TEST(NrActorScheduleGateTests, NewAdmissionCommitRequestsReadyPublish)
        {
            NrActorScheduleGate gate;
            NrActorAdmissionTicket ticket = gate.TryBeginAdmission();

            const NrActorScheduleDirective directive =
                gate.CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);

            EXPECT_EQ(directive, NrActorScheduleDirective::EnqueueReadyToken);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Scheduled);
        }

        TEST(NrActorScheduleGateTests, ConcurrentNewAdmissionIsRejectedWithoutChangingOwnerState)
        {
            NrActorScheduleGate gate;
            NrActorAdmissionTicket owner = gate.TryBeginAdmission();

            const NrActorAdmissionTicket follower = gate.TryBeginAdmission();

            ASSERT_TRUE(owner.Accepted());
            EXPECT_FALSE(follower.Accepted());
            EXPECT_EQ(gate.View(), NrActorScheduleView::Admitting);
            EXPECT_EQ(gate.CompleteAdmission(std::move(owner), NrActorAdmissionResolution::CapacityRejected),
                      NrActorScheduleDirective::NoAction);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Idle);
        }

        TEST(NrActorScheduleGateTests, ScheduledActorAdmissionUsesExistingPermit)
        {
            NrActorScheduleGate gate;
            ScheduleActor(gate);

            NrActorAdmissionTicket ticket = gate.TryBeginAdmission();
            ASSERT_TRUE(ticket.Accepted());
            EXPECT_FALSE(ticket.NeedsNewPermit());

            const NrActorScheduleDirective directive =
                gate.CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::MailboxCommitted);

            EXPECT_EQ(directive, NrActorScheduleDirective::NoAction);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Scheduled);
        }

        TEST(NrActorScheduleGateTests, DrainCompletionDefersUntilInFlightProducerCommits)
        {
            NrActorScheduleGate gate;
            ScheduleActor(gate);
            ASSERT_TRUE(gate.TryBeginDrain());

            NrActorAdmissionTicket producer = gate.TryBeginAdmission();

            EXPECT_EQ(gate.CompleteDrain(false), NrActorScheduleDirective::FinalizationDeferred);
            EXPECT_EQ(gate.View(), NrActorScheduleView::DrainCompleting);

            EXPECT_EQ(gate.CompleteAdmission(std::move(producer), NrActorAdmissionResolution::MailboxCommitted),
                      NrActorScheduleDirective::EnqueueReadyToken);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Scheduled);
        }

        TEST(NrActorScheduleGateTests, DrainCompletionReleasesPermitAfterLastProducerRejects)
        {
            NrActorScheduleGate gate;
            ScheduleActor(gate);
            ASSERT_TRUE(gate.TryBeginDrain());

            NrActorAdmissionTicket producer = gate.TryBeginAdmission();

            EXPECT_EQ(gate.CompleteDrain(false), NrActorScheduleDirective::FinalizationDeferred);
            EXPECT_EQ(gate.CompleteAdmission(std::move(producer), NrActorAdmissionResolution::MailboxRejected),
                      NrActorScheduleDirective::ReleasePermit);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Idle);
        }

        TEST(NrActorScheduleGateTests, LastConcurrentProducerFinalizesDeferredDrain)
        {
            NrActorScheduleGate gate;
            ScheduleActor(gate);
            ASSERT_TRUE(gate.TryBeginDrain());

            NrActorAdmissionTicket first = gate.TryBeginAdmission();
            NrActorAdmissionTicket second = gate.TryBeginAdmission();

            ASSERT_TRUE(first.Accepted());
            ASSERT_TRUE(second.Accepted());
            EXPECT_EQ(gate.CompleteDrain(false), NrActorScheduleDirective::FinalizationDeferred);
            EXPECT_EQ(gate.CompleteAdmission(std::move(first), NrActorAdmissionResolution::MailboxCommitted),
                      NrActorScheduleDirective::NoAction);
            EXPECT_EQ(gate.View(), NrActorScheduleView::DrainCompleting);
            EXPECT_EQ(gate.CompleteAdmission(std::move(second), NrActorAdmissionResolution::MailboxRejected),
                      NrActorScheduleDirective::EnqueueReadyToken);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Scheduled);
        }

        TEST(NrActorScheduleGateTests, DrainBudgetRescheduleSurvivesProducerRejection)
        {
            NrActorScheduleGate gate;
            ScheduleActor(gate);
            ASSERT_TRUE(gate.TryBeginDrain());

            NrActorAdmissionTicket producer = gate.TryBeginAdmission();

            EXPECT_EQ(gate.CompleteDrain(true), NrActorScheduleDirective::FinalizationDeferred);
            EXPECT_EQ(gate.CompleteAdmission(std::move(producer), NrActorAdmissionResolution::MailboxRejected),
                      NrActorScheduleDirective::EnqueueReadyToken);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Scheduled);
        }

        TEST(NrActorScheduleGateTests, AdmissionDuringDrainPreservesPendingReschedule)
        {
            NrActorScheduleGate gate;
            ScheduleActor(gate);
            ASSERT_TRUE(gate.TryBeginDrain());

            NrActorAdmissionTicket producer = gate.TryBeginAdmission();
            ASSERT_TRUE(producer.Accepted());
            ASSERT_FALSE(producer.NeedsNewPermit());
            EXPECT_EQ(gate.CompleteAdmission(std::move(producer), NrActorAdmissionResolution::MailboxCommitted),
                      NrActorScheduleDirective::NoAction);
            EXPECT_EQ(gate.View(), NrActorScheduleView::DrainingWithPending);
            EXPECT_EQ(gate.CompleteDrain(false), NrActorScheduleDirective::EnqueueReadyToken);
            EXPECT_EQ(gate.View(), NrActorScheduleView::Scheduled);
        }
    } // namespace
} // namespace psnr::core
