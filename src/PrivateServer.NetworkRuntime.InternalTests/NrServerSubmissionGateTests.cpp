#include "pch.h"

#include "NrServerSubmissionGate.h"

#include <atomic>
#include <thread>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    namespace
    {
        [[nodiscard]] NrServerSubmissionGate CreateGate()
        {
            auto result = NrServerSubmissionGate::Create();
            EXPECT_TRUE(result.Succeeded());
            if (result.Failed())
            {
                return NrServerSubmissionGate();
            }

            return result.TakeValue();
        }

        TEST(NrServerSubmissionGateTests, DefaultGateHandleAndPermitAreInvalid)
        {
            const NrServerSubmissionGate gate;
            const NrSubmissionAdmissionHandle handle;
            const NrSubmissionPermit permit;

            EXPECT_FALSE(gate.IsValid());
            EXPECT_FALSE(handle.IsValid());
            EXPECT_FALSE(handle.IsAccepting());
            EXPECT_EQ(handle.InFlightCount(), 0u);
            EXPECT_FALSE(permit.IsValid());
        }

        TEST(NrServerSubmissionGateTests, PermitTracksInFlightAdmissionAndMoveTransfersOwnership)
        {
            NrServerSubmissionGate gate = CreateGate();
            NrSubmissionAdmissionHandle handle = gate.CreateAdmissionHandle();
            NrSubmissionPermit permit;

            ASSERT_TRUE(handle.TryAcquirePermit(permit).Succeeded());
            EXPECT_TRUE(permit.IsValid());
            EXPECT_EQ(handle.InFlightCount(), 1u);

            NrSubmissionPermit moved = std::move(permit);
            EXPECT_FALSE(permit.IsValid());
            EXPECT_TRUE(moved.IsValid());
            EXPECT_EQ(handle.InFlightCount(), 1u);

            moved.Reset();
            EXPECT_EQ(handle.InFlightCount(), 0u);
        }

        TEST(NrServerSubmissionGateTests, AcquireRejectsValidOutputWithoutChangingAdmission)
        {
            NrServerSubmissionGate gate = CreateGate();
            NrSubmissionAdmissionHandle handle = gate.CreateAdmissionHandle();
            NrSubmissionPermit permit;
            ASSERT_TRUE(handle.TryAcquirePermit(permit).Succeeded());

            const NrStatus status = handle.TryAcquirePermit(permit);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_TRUE(permit.IsValid());
            EXPECT_EQ(handle.InFlightCount(), 1u);
        }

        TEST(NrServerSubmissionGateTests, ServerInvalidationRejectsPermitButCopiedHandleRemainsSafe)
        {
            NrServerSubmissionGate gate = CreateGate();
            NrSubmissionAdmissionHandle handle = gate.CreateAdmissionHandle();
            NrSubmissionAdmissionHandle copied = handle;

            ASSERT_TRUE(gate.InvalidateAndWait().Succeeded());
            EXPECT_FALSE(handle.IsAccepting());
            EXPECT_FALSE(copied.IsAccepting());

            NrSubmissionPermit permit;
            const NrStatus status = copied.TryAcquirePermit(permit);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_FALSE(permit.IsValid());
            EXPECT_EQ(copied.InFlightCount(), 0u);
        }

        TEST(NrServerSubmissionGateTests, PermitMatchesOnlyHandlesForTheSameServerGate)
        {
            NrServerSubmissionGate firstGate = CreateGate();
            NrServerSubmissionGate secondGate = CreateGate();
            NrSubmissionAdmissionHandle first = firstGate.CreateAdmissionHandle();
            NrSubmissionAdmissionHandle firstCopy = first;
            NrSubmissionAdmissionHandle second = secondGate.CreateAdmissionHandle();
            NrSubmissionPermit permit;
            ASSERT_TRUE(first.TryAcquirePermit(permit).Succeeded());

            EXPECT_TRUE(first.MatchesPermit(permit));
            EXPECT_TRUE(firstCopy.MatchesPermit(permit));
            EXPECT_FALSE(second.MatchesPermit(permit));
            EXPECT_FALSE(NrSubmissionAdmissionHandle().MatchesPermit(permit));
        }

        TEST(NrServerSubmissionGateTests, GateDestructionInvalidatesOutstandingAdmissionHandle)
        {
            NrSubmissionAdmissionHandle handle;
            {
                NrServerSubmissionGate gate = CreateGate();
                handle = gate.CreateAdmissionHandle();
                ASSERT_TRUE(handle.IsAccepting());
            }

            EXPECT_TRUE(handle.IsValid());
            EXPECT_FALSE(handle.IsAccepting());

            NrSubmissionPermit permit;
            EXPECT_EQ(handle.TryAcquirePermit(permit).ErrorCode(), NrErrorCode::InvalidState);
            EXPECT_FALSE(permit.IsValid());
        }

        TEST(NrServerSubmissionGateTests, InvalidateWaitsOnlyForOutstandingPermit)
        {
            NrServerSubmissionGate gate = CreateGate();
            NrSubmissionAdmissionHandle handle = gate.CreateAdmissionHandle();
            NrSubmissionPermit permit;
            ASSERT_TRUE(handle.TryAcquirePermit(permit).Succeeded());

            std::atomic_bool invalidationFinished{false};
            NrStatus invalidationStatus = NrStatus::Failure(NrErrorCode::InvalidState);
            std::thread invalidationThread([&gate, &invalidationFinished, &invalidationStatus]() {
                invalidationStatus = gate.InvalidateAndWait();
                invalidationFinished.store(true, std::memory_order_release);
            });

            while (handle.IsAccepting())
            {
                std::this_thread::yield();
            }

            EXPECT_FALSE(invalidationFinished.load(std::memory_order_acquire));
            EXPECT_EQ(handle.InFlightCount(), 1u);

            permit.Reset();
            invalidationThread.join();

            EXPECT_TRUE(invalidationStatus.Succeeded());
            EXPECT_TRUE(invalidationFinished.load(std::memory_order_acquire));
            EXPECT_EQ(handle.InFlightCount(), 0u);
        }
    } // namespace
} // namespace psnr::runtime::internal
