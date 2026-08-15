#pragma once

#include "NrActor.h"
#include "NrActorScheduleGate.h"

namespace psnr::core
{
    class NrActorExecutor
    {
    public:
        explicit NrActorExecutor(NrActorDrainBudget drainBudget = {}) noexcept;

        [[nodiscard]] NrActorDrainBudget DrainBudget() const noexcept;
        [[nodiscard]] NrActorRunReport TryRun(INrActor& actor, NrActorScheduleGate& scheduleGate) noexcept;

    private:
        NrActorDrainBudget drainBudget_;
    };
} // namespace psnr::core
