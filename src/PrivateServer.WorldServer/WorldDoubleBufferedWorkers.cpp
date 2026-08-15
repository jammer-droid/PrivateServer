#include "pch.h"

#include "WorldDoubleBufferedWorkers.h"

namespace psnr::world
{
    WorldIngressWorkerExchangeResult WorldIngressWorkerExchange::Publish(const WorldIngressWorkerPlan& plan) noexcept
    {
        if (plan.epoch == 0)
        {
            return WorldIngressWorkerExchangeResult::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (closed_)
        {
            return WorldIngressWorkerExchangeResult::Closed;
        }
        if (planReady_ || completionReady_)
        {
            return WorldIngressWorkerExchangeResult::InvalidState;
        }

        plan_ = plan;
        planReady_ = true;
        condition_.notify_all();
        return WorldIngressWorkerExchangeResult::Exchanged;
    }

    WorldIngressWorkerExchangeResult WorldIngressWorkerExchange::WaitTakePlan(
        WorldIngressWorkerPlan* const outPlan) noexcept
    {
        if (outPlan == nullptr)
        {
            return WorldIngressWorkerExchangeResult::InvalidArgument;
        }

        std::unique_lock<std::mutex> lock{mutex_};
        condition_.wait(lock, [this]() noexcept { return planReady_ || closed_; });
        if (!planReady_)
        {
            return WorldIngressWorkerExchangeResult::Closed;
        }

        *outPlan = plan_;
        planReady_ = false;
        return WorldIngressWorkerExchangeResult::Exchanged;
    }

    WorldIngressWorkerExchangeResult WorldIngressWorkerExchange::Complete(
        const WorldIngressWorkerCompletion& completion) noexcept
    {
        if (completion.epoch == 0)
        {
            return WorldIngressWorkerExchangeResult::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (closed_)
        {
            return WorldIngressWorkerExchangeResult::Closed;
        }
        if (planReady_ || completionReady_)
        {
            return WorldIngressWorkerExchangeResult::InvalidState;
        }

        completion_ = completion;
        completionReady_ = true;
        condition_.notify_all();
        return WorldIngressWorkerExchangeResult::Exchanged;
    }

    WorldIngressWorkerExchangeResult WorldIngressWorkerExchange::WaitTakeCompletion(
        const std::uint64_t epoch, WorldIngressWorkerCompletion* const outCompletion) noexcept
    {
        if (epoch == 0 || outCompletion == nullptr)
        {
            return WorldIngressWorkerExchangeResult::InvalidArgument;
        }

        std::unique_lock<std::mutex> lock{mutex_};
        condition_.wait(lock, [this]() noexcept { return completionReady_ || closed_; });
        if (!completionReady_)
        {
            return WorldIngressWorkerExchangeResult::Closed;
        }
        if (completion_.epoch != epoch)
        {
            return WorldIngressWorkerExchangeResult::InvalidState;
        }

        *outCompletion = completion_;
        completionReady_ = false;
        return WorldIngressWorkerExchangeResult::Exchanged;
    }

    void WorldIngressWorkerExchange::Close() noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        closed_ = true;
        condition_.notify_all();
    }

} // namespace psnr::world
