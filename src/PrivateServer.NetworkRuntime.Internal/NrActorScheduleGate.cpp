#include "pch.h"

#include "NrActorScheduleGate.h"

#include <cassert>

namespace psnr::core
{
    NrActorScheduleView NrActorScheduleGate::View() const noexcept
    {
        return DecodeView(scheduleState_.load(std::memory_order_acquire));
    }

    NrActorAdmissionTicket NrActorScheduleGate::TryBeginAdmission() noexcept
    {
        NrActorScheduleStateWord observed = scheduleState_.load(std::memory_order_acquire);

        while (true)
        {
            const NrPackedPhase phase = PackedPhase(observed);
            const std::uint32_t producerInFlight = ProducerInFlight(observed);

            if (phase == NrPackedPhase::Admitting || producerInFlight == MaxProducerInFlight)
            {
                return NrActorAdmissionTicket::Rejected();
            }

            NrActorScheduleStateWord desired = observed;
            NrActorAdmissionDecision admissionDecision = NrActorAdmissionDecision::Rejected;

            if (phase == NrPackedPhase::Idle) // idle actor
            {
                // Idle actor의 첫 producer만 새 runnable permit 예약 단계로 진입한다.
                desired = ClearDrainFlags(desired);
                desired = WithPhase(desired, NrPackedPhase::Admitting); // to Admitting
                desired = WithProducerInFlight(desired, 1);
                admissionDecision = NrActorAdmissionDecision::RequiresNewPermit;
            }
            else // non-idle actor
            {
                // Scheduled/Draining actor는 기존 permit을 pin하고 mailbox admission에 참여한다.
                // Phase는 기존 Phase를 그대로 사용함
                desired = WithProducerInFlight(desired, producerInFlight + 1);
                admissionDecision = NrActorAdmissionDecision::UsesExistingPermit;
            }

            if (scheduleState_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
            {
                return NrActorAdmissionTicket(admissionDecision);
            }
        }
    }

    NrActorScheduleDirective NrActorScheduleGate::CompleteAdmission(NrActorAdmissionTicket&& ticket,
                                                                    NrActorAdmissionResolution resolution) noexcept
    {
        if (!ticket.Accepted())
        {
            return NrActorScheduleDirective::NoAction;
        }

        const NrActorAdmissionDecision admissionDecision = ticket.decision_;
        ticket.decision_ = NrActorAdmissionDecision::Rejected;

        NrActorScheduleStateWord observed = scheduleState_.load(std::memory_order_acquire);

        while (true)
        {
            const NrPackedPhase phase = PackedPhase(observed);
            const std::uint32_t producerInFlight = ProducerInFlight(observed);

            assert(producerInFlight > 0); // ticket 은 항상 유효해야 함

            if (producerInFlight == 0) // admission transaction 진행중인 producer가 없음, invalid.
            {
                // producerInFlight - 1 연산을 피하기 위해 early return
                return NrActorScheduleDirective::NoAction;
            }

            NrActorScheduleStateWord desired = observed;
            NrActorScheduleDirective directive = NrActorScheduleDirective::NoAction;

            if (admissionDecision == NrActorAdmissionDecision::RequiresNewPermit) // idle actor case
            {
                if (phase != NrPackedPhase::Admitting || producerInFlight != 1)
                {
                    return NrActorScheduleDirective::NoAction;
                }

                desired = ClearDrainFlags(desired);
                desired = WithProducerInFlight(desired, 0); // clear inFlight

                switch (resolution)
                {
                case NrActorAdmissionResolution::CapacityRejected:
                    desired = WithPhase(desired, NrPackedPhase::Idle);
                    break;

                case NrActorAdmissionResolution::MailboxRejected:
                    desired = WithPhase(desired, NrPackedPhase::Idle);
                    directive = NrActorScheduleDirective::ReleasePermit;
                    break;

                case NrActorAdmissionResolution::MailboxCommitted:
                    desired = WithPhase(desired, NrPackedPhase::Scheduled);
                    directive = NrActorScheduleDirective::EnqueueReadyToken;
                    break;
                }
            }
            else // non-idle actor case
            {
                if (resolution == NrActorAdmissionResolution::CapacityRejected || phase == NrPackedPhase::Idle ||
                    phase == NrPackedPhase::Admitting)
                {
                    return NrActorScheduleDirective::NoAction;
                }

                desired = WithProducerInFlight(desired, producerInFlight - 1);

                if (resolution == NrActorAdmissionResolution::MailboxCommitted &&
                    (phase == NrPackedPhase::Draining || phase == NrPackedPhase::DrainCompleting))
                {
                    desired |= PendingBit; // pending bit on with MailboxComitted (only place to On PendingBit)
                }

                if (phase == NrPackedPhase::DrainCompleting && producerInFlight == 1)
                {
                    // worker의 drain 은 완료 && producerInFlight > 0 -> Idle / Scheduled 상태 전이 보류
                    // producerInFlight == 1 -> CompleteAdmission을 호출한 producer 하나만 남은 상태

                    const bool needsReschedule =
                        HasBit(desired, PendingBit) || HasBit(desired, DrainNeedsRescheduleBit);

                    desired = ClearDrainFlags(desired);
                    desired = WithPhase(desired, needsReschedule ? NrPackedPhase::Scheduled : NrPackedPhase::Idle);
                    directive = needsReschedule ? NrActorScheduleDirective::EnqueueReadyToken
                                                : NrActorScheduleDirective::ReleasePermit;
                }
            }

            if (scheduleState_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
            {
                return directive;
            }
        }
    }

    bool NrActorScheduleGate::TryBeginDrain() noexcept
    {
        NrActorScheduleStateWord observed = scheduleState_.load(std::memory_order_acquire);

        while (PackedPhase(observed) == NrPackedPhase::Scheduled)
        {
            NrActorScheduleStateWord desired = ClearDrainFlags(observed);
            desired = WithPhase(desired, NrPackedPhase::Draining);

            if (scheduleState_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
            {
                return true;
            }
        }

        return false;
    }

    NrActorScheduleDirective NrActorScheduleGate::CompleteDrain(bool shouldReschedule) noexcept
    {
        NrActorScheduleStateWord observed = scheduleState_.load(std::memory_order_acquire);

        while (true)
        {
            // Draining 상태만 허용, 나머지 Phase는 invariant
            const NrPackedPhase phase = PackedPhase(observed);

            assert(phase == NrPackedPhase::Draining);

            if (phase != NrPackedPhase::Draining)
            {
                return NrActorScheduleDirective::NoAction;
            }

            const std::uint32_t producerInFlight = ProducerInFlight(observed);
            NrActorScheduleStateWord desired = observed;
            if (shouldReschedule)
            {
                desired |= DrainNeedsRescheduleBit; // only place to On DrainNeedsRecheduleBit('R'escheduleBit)
            }

            NrActorScheduleDirective directive = NrActorScheduleDirective::NoAction;
            if (producerInFlight > 0)
            {
                // worker는 기다리지 않는다. 마지막 producer가 commit/abort 결과로 상태를 확정한다.
                desired = WithPhase(desired, NrPackedPhase::DrainCompleting);
                directive = NrActorScheduleDirective::FinalizationDeferred;
            }
            else
            {
                const bool needsReschedule = HasBit(desired, PendingBit) || HasBit(desired, DrainNeedsRescheduleBit);
                desired = ClearDrainFlags(desired);
                desired = WithPhase(desired, needsReschedule ? NrPackedPhase::Scheduled : NrPackedPhase::Idle);
                directive = needsReschedule ? NrActorScheduleDirective::EnqueueReadyToken
                                            : NrActorScheduleDirective::ReleasePermit;
            }

            if (scheduleState_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
            {
                return directive;
            }
        }
    }

    NrActorScheduleGate::NrPackedPhase NrActorScheduleGate::PackedPhase(NrActorScheduleStateWord state) noexcept
    {
        return static_cast<NrPackedPhase>(state & PhaseMask);
    }

    std::uint32_t NrActorScheduleGate::ProducerInFlight(NrActorScheduleStateWord state) noexcept
    {
        return static_cast<std::uint32_t>((state & ProducerInFlightMask) >> ProducerInFlightShift);
    }

    bool NrActorScheduleGate::HasBit(NrActorScheduleStateWord state, NrActorScheduleStateWord bit) noexcept
    {
        return (state & bit) != 0;
    }

    NrActorScheduleGate::NrActorScheduleStateWord NrActorScheduleGate::WithPhase(NrActorScheduleStateWord state,
                                                                                 NrPackedPhase phase) noexcept
    {
        return (state & ~PhaseMask) | static_cast<NrActorScheduleStateWord>(phase);
    }

    NrActorScheduleGate::NrActorScheduleStateWord NrActorScheduleGate::WithProducerInFlight(
        NrActorScheduleStateWord state, std::uint32_t count) noexcept
    {
        return (state & ~ProducerInFlightMask) |
               (static_cast<NrActorScheduleStateWord>(count) << ProducerInFlightShift);
    }

    NrActorScheduleGate::NrActorScheduleStateWord NrActorScheduleGate::ClearDrainFlags(
        NrActorScheduleStateWord state) noexcept
    {
        return state & ~(PendingBit | DrainNeedsRescheduleBit); // clear R|P
    }

    NrActorScheduleView NrActorScheduleGate::DecodeView(NrActorScheduleStateWord state) noexcept
    {
        switch (PackedPhase(state))
        {
        case NrPackedPhase::Idle:
            return NrActorScheduleView::Idle;
        case NrPackedPhase::Admitting:
            return NrActorScheduleView::Admitting;
        case NrPackedPhase::Scheduled:
            return NrActorScheduleView::Scheduled;
        case NrPackedPhase::Draining:
            return HasBit(state, PendingBit) ? NrActorScheduleView::DrainingWithPending : NrActorScheduleView::Draining;
        case NrPackedPhase::DrainCompleting:
            return NrActorScheduleView::DrainCompleting;
        }

        return NrActorScheduleView::Idle;
    }
} // namespace psnr::core
