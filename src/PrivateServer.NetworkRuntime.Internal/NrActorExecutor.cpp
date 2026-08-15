#include "pch.h"

#include "NrActorExecutor.h"

namespace psnr::core
{
    NrActorExecutor::NrActorExecutor(NrActorDrainBudget drainBudget) noexcept
        : drainBudget_(drainBudget)
    {
    }

    NrActorDrainBudget NrActorExecutor::DrainBudget() const noexcept
    {
        return drainBudget_;
    }

    NrActorRunReport NrActorExecutor::TryRun(INrActor& actor, NrActorScheduleGate& scheduleGate) noexcept
    {
        if (!scheduleGate.TryBeginDrain())
        {
            return NrActorRunReport::AlreadyDraining();
        }

        NrResult<NrActorDrainReport> drainResult = actor.Drain(drainBudget_);
        const bool shouldReschedule = drainResult.Succeeded() && drainResult.Value().needsReschedule;
        const NrActorScheduleDirective scheduleDirective = scheduleGate.CompleteDrain(shouldReschedule);

        if (drainResult.Failed())
        {
            return NrActorRunReport::DrainFailed(drainResult.Status(), scheduleDirective);
        }

        return NrActorRunReport::Drained(drainResult.Value(), scheduleDirective);
    }
} // namespace psnr::core
