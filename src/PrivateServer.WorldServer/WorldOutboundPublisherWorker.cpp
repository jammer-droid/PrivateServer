#include "pch.h"

#include "WorldDoubleBufferedWorkers.h"

#include <system_error>

namespace psnr::world
{
    WorldOutboundPublisherWorker::WorldOutboundPublisherWorker(WorldOutboundPublisher& publisher,
                                                               psnr::runtime::NrGateway& gateway) noexcept
        : publisher_(publisher)
        , gateway_(gateway)
    {
    }

    WorldOutboundPublisherWorker::~WorldOutboundPublisherWorker()
    {
        RequestStop();
        Join();
    }

    bool WorldOutboundPublisherWorker::Start() noexcept
    {
        if (started_.exchange(true, std::memory_order_acq_rel))
        {
            return false;
        }

        try
        {
            stopReason_.store(WorldConcreteWorkerStopReason::Running, std::memory_order_release);
            thread_ = std::thread{&WorldOutboundPublisherWorker::Run, this};
        }
        catch (const std::system_error&)
        {
            started_.store(false, std::memory_order_release);
            stopReason_.store(WorldConcreteWorkerStopReason::StartFailed, std::memory_order_release);
            return false;
        }
        return true;
    }

    void WorldOutboundPublisherWorker::RequestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        publisher_.Close();
    }

    bool WorldOutboundPublisherWorker::DrainAndStop() noexcept
    {
        drainRequested_.store(true, std::memory_order_release);
        publisher_.FinishWrites();
        Join();
        const WorldConcreteWorkerStopReason stopReason = stopReason_.load(std::memory_order_acquire);
        return stopReason == WorldConcreteWorkerStopReason::Completed ||
               stopReason == WorldConcreteWorkerStopReason::StopRequested;
    }

    void WorldOutboundPublisherWorker::Join() noexcept
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    WorldConcreteWorkerStopReason WorldOutboundPublisherWorker::StopReason() const noexcept
    {
        return stopReason_.load(std::memory_order_acquire);
    }

    WorldOutboundPublishReport WorldOutboundPublisherWorker::LastReport() const noexcept
    {
        std::lock_guard<std::mutex> lock{reportMutex_};
        return lastReport_;
    }

    void WorldOutboundPublisherWorker::Run() noexcept
    {
        constexpr std::chrono::milliseconds WaitTimeout{100};
        while (true)
        {
            const WorldOutboundPublishReport report = publisher_.PublishNext(gateway_, WaitTimeout);
            {
                std::lock_guard<std::mutex> lock{reportMutex_};
                lastReport_ = report;
            }

            if (report.stopReason == WorldOutboundPublishStopReason::NoBatch)
            {
                if (drainRequested_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire))
                {
                    break;
                }
                continue;
            }
            if (report.stopReason != WorldOutboundPublishStopReason::Published)
            {
                if (stopRequested_.load(std::memory_order_acquire))
                {
                    break;
                }
                stopReason_.store(WorldConcreteWorkerStopReason::OperationFailed, std::memory_order_release);
                publisher_.Close();
                return;
            }
        }

        publisher_.Close();
        stopReason_.store(drainRequested_.load(std::memory_order_acquire)
                              ? WorldConcreteWorkerStopReason::Completed
                              : WorldConcreteWorkerStopReason::StopRequested,
                          std::memory_order_release);
    }
} // namespace psnr::world
