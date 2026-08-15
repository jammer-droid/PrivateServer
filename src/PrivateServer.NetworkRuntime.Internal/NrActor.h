#pragma once

#include "NrActorScheduleGate.h"
#include "NrResult.h"

#include <cstddef>

namespace psnr::core
{
    struct NrActorDrainBudget
    {
        std::size_t maxEvents = 1;
    };

    struct NrActorDrainReport // Actor가 mailbox를 drain한 결과 payload
    {
        std::size_t drainedCount = 0;
        bool needsReschedule = false;
    };

    enum class NrActorRunState
    {
        Drained,
        AlreadyDraining,
    };

    struct NrActorRunReport // Executor가 Actor 실행을 시도한 결과 payload
    {
        NrActorRunState runState = NrActorRunState::AlreadyDraining;
        std::size_t drainedCount = 0;
        NrActorScheduleDirective scheduleDirective = NrActorScheduleDirective::NoAction;
        NrStatus status;

        [[nodiscard]] static NrActorRunReport Drained(const NrActorDrainReport& drainReport,
                                                      NrActorScheduleDirective directive) noexcept
        {
            return NrActorRunReport{NrActorRunState::Drained, drainReport.drainedCount, directive, NrStatus::Success()};
        }

        [[nodiscard]] static NrActorRunReport DrainFailed(NrStatus failureStatus,
                                                          NrActorScheduleDirective directive) noexcept
        {
            return NrActorRunReport{NrActorRunState::Drained, 0, directive, failureStatus};
        }

        [[nodiscard]] static NrActorRunReport AlreadyDraining() noexcept
        {
            return NrActorRunReport{};
        }
    };

    class INrActor
    {
    public:
        INrActor() = default;

        INrActor(const INrActor&) = delete;
        INrActor& operator=(const INrActor&) = delete;

        INrActor(INrActor&&) = delete;
        INrActor& operator=(INrActor&&) = delete;

        virtual ~INrActor() noexcept = default;

        [[nodiscard]] virtual NrResult<NrActorDrainReport> Drain(NrActorDrainBudget budget) noexcept = 0;
    };
} // namespace psnr::core
