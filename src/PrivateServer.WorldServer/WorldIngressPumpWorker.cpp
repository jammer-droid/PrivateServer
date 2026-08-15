#include "pch.h"

#include "WorldDoubleBufferedWorkers.h"

#include <system_error>

namespace psnr::world
{
    WorldIngressPumpWorker::WorldIngressPumpWorker(WorldIngressPump& pump, NrServerWorldEventSource& source,
                                                   WorldIngressWorkerExchange& exchange) noexcept
        : pump_(pump)
        , source_(source)
        , exchange_(exchange)
    {
    }

    WorldIngressPumpWorker::~WorldIngressPumpWorker()
    {
        RequestStop();
        Join();
    }

    bool WorldIngressPumpWorker::Start() noexcept
    {
        if (started_.exchange(true, std::memory_order_acq_rel))
        {
            return false;
        }

        try
        {
            stopReason_.store(WorldConcreteWorkerStopReason::Running, std::memory_order_release);
            thread_ = std::thread{&WorldIngressPumpWorker::Run, this};
        }
        catch (const std::system_error&)
        {
            started_.store(false, std::memory_order_release);
            stopReason_.store(WorldConcreteWorkerStopReason::StartFailed, std::memory_order_release);
            return false;
        }
        return true;
    }

    void WorldIngressPumpWorker::RequestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        pump_.Close();
        exchange_.Close();
    }

    void WorldIngressPumpWorker::EnterTerminalDrain() noexcept
    {
        terminalDrainEnabled_.store(true, std::memory_order_release);
    }

    void WorldIngressPumpWorker::Join() noexcept
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    WorldConcreteWorkerStopReason WorldIngressPumpWorker::StopReason() const noexcept
    {
        return stopReason_.load(std::memory_order_acquire);
    }

    bool WorldIngressPumpWorker::TerminalDrainSucceeded() const noexcept
    {
        return terminalDrainEnabled_.load(std::memory_order_acquire) &&
               stopReason_.load(std::memory_order_acquire) == WorldConcreteWorkerStopReason::Completed;
    }

    void WorldIngressPumpWorker::Run() noexcept
    {
        constexpr std::chrono::milliseconds NormalWaitTimeout{100};
        while (!stopRequested_.load(std::memory_order_acquire) &&
               !terminalDrainEnabled_.load(std::memory_order_acquire))
        {
            const WorldIngressPumpStepReport report = pump_.RunStep(source_, NormalWaitTimeout);
            if (report.stopReason == WorldIngressPumpStepStopReason::Progressed ||
                report.stopReason == WorldIngressPumpStepStopReason::Idle ||
                report.stopReason == WorldIngressPumpStepStopReason::SlotFull)
            {
                continue;
            }
            if (report.stopReason == WorldIngressPumpStepStopReason::SourceClosed &&
                terminalDrainEnabled_.load(std::memory_order_acquire))
            {
                break;
            }

            stopReason_.store(WorldConcreteWorkerStopReason::OperationFailed, std::memory_order_release);
            exchange_.Close();
            return;
        }

        while (!stopRequested_.load(std::memory_order_acquire))
        {
            WorldIngressWorkerPlan plan;
            if (exchange_.WaitTakePlan(&plan) != WorldIngressWorkerExchangeResult::Exchanged)
            {
                break;
            }

            WorldIngressWorkerCompletion completion;
            completion.epoch = plan.epoch;
            completion.terminalReport = pump_.RunTerminalEpoch(source_, clockSource_, plan.epoch, plan.deadline);
            if (exchange_.Complete(completion) != WorldIngressWorkerExchangeResult::Exchanged)
            {
                break;
            }
            if (completion.terminalReport.stopReason == WorldIngressTerminalEpochStopReason::SealedFull)
            {
                continue;
            }
            if (completion.terminalReport.stopReason == WorldIngressTerminalEpochStopReason::SourceClosed)
            {
                stopReason_.store(WorldConcreteWorkerStopReason::Completed, std::memory_order_release);
                exchange_.Close();
                return;
            }

            stopReason_.store(WorldConcreteWorkerStopReason::OperationFailed, std::memory_order_release);
            exchange_.Close();
            return;
        }

        stopReason_.store(WorldConcreteWorkerStopReason::StopRequested, std::memory_order_release);
        exchange_.Close();
    }

} // namespace psnr::world
