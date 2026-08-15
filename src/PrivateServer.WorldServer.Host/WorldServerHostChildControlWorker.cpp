#include "WorldServerHostChildControlWorker.h"

#include "WorldServerHostStopSignal.h"

#define NOMINMAX
#include <Windows.h>

#include <system_error>
#include <utility>

namespace psnr::world::host
{
    WorldServerHostChildControlWorker::WorldServerHostChildControlWorker(WorldServerHostChildControl& control,
                                                                         WorldServerHostStopSignal& stopSignal) noexcept
        : control_(control)
        , stopSignal_(stopSignal)
    {
    }

    WorldServerHostChildControlWorker::~WorldServerHostChildControlWorker()
    {
        RequestStop();
        Join();
    }

    bool WorldServerHostChildControlWorker::Start() noexcept
    {
        if (started_.exchange(true, std::memory_order_acq_rel))
        {
            return false;
        }

        stopRequested_.store(false, std::memory_order_release);
        result_.store(WorldServerHostChildControlWorkerResult::Running, std::memory_order_release);
        try
        {
            thread_ = std::thread{&WorldServerHostChildControlWorker::Run, this};
        }
        catch (const std::system_error&)
        {
            started_.store(false, std::memory_order_release);
            result_.store(WorldServerHostChildControlWorkerResult::StartFailed, std::memory_order_release);
            return false;
        }
        return true;
    }

    void WorldServerHostChildControlWorker::RequestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        CancelPendingRead();
        shutdownCondition_.notify_all();
    }

    void WorldServerHostChildControlWorker::NotifyShutdownCompleted(const bool succeeded) noexcept
    {
        {
            const std::lock_guard<std::mutex> lock{shutdownMutex_};
            shutdownCompleted_ = true;
            shutdownSucceeded_ = succeeded;
        }
        shutdownCondition_.notify_all();
    }

    void WorldServerHostChildControlWorker::Join() noexcept
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    WorldServerHostChildControlWorkerResult WorldServerHostChildControlWorker::Result() const noexcept
    {
        return result_.load(std::memory_order_acquire);
    }

    std::uint64_t WorldServerHostChildControlWorker::StopSequence() const noexcept
    {
        return stopSequence_;
    }

    const WorldServerHostChildControlFailure& WorldServerHostChildControlWorker::Failure() const noexcept
    {
        return failure_;
    }

    void WorldServerHostChildControlWorker::Run() noexcept
    {
        if (stopRequested_.load(std::memory_order_acquire))
        {
            result_.store(WorldServerHostChildControlWorkerResult::Cancelled, std::memory_order_release);
            return;
        }

        WorldResult<void, WorldServerHostChildControlFailure> readyResult = control_.WriteReady();
        if (readyResult.Failed())
        {
            failure_ = readyResult.Error();
            result_.store(WorldServerHostChildControlWorkerResult::ReadyWriteFailed, std::memory_order_release);
            stopSignal_.Request();
            return;
        }

        reading_.store(true, std::memory_order_release);
        if (stopRequested_.load(std::memory_order_acquire))
        {
            reading_.store(false, std::memory_order_release);
            result_.store(WorldServerHostChildControlWorkerResult::Cancelled, std::memory_order_release);
            return;
        }

        WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure> commandResult =
            control_.ReadCommand();
        reading_.store(false, std::memory_order_release);
        if (commandResult.Failed())
        {
            if (stopRequested_.load(std::memory_order_acquire))
            {
                result_.store(WorldServerHostChildControlWorkerResult::Cancelled, std::memory_order_release);
                return;
            }

            failure_ = commandResult.Error();
            result_.store(WorldServerHostChildControlWorkerResult::ReadFailed, std::memory_order_release);
            stopSignal_.Request();
            return;
        }

        stopSequence_ = commandResult.Value().sequence;
        result_.store(WorldServerHostChildControlWorkerResult::StopReceived, std::memory_order_release);
        stopSignal_.Request();

        bool shutdownSucceeded = false;
        {
            std::unique_lock<std::mutex> lock{shutdownMutex_};
            shutdownCondition_.wait(lock, [this]()
                                    { return shutdownCompleted_ || stopRequested_.load(std::memory_order_acquire); });
            if (stopRequested_.load(std::memory_order_acquire))
            {
                result_.store(WorldServerHostChildControlWorkerResult::Cancelled, std::memory_order_release);
                return;
            }
            shutdownSucceeded = shutdownSucceeded_;
        }

        if (!shutdownSucceeded)
        {
            WorldServerHostChildControlFailure shutdownFailure;
            shutdownFailure.message = "host shutdown failed";
            WorldResult<void, WorldServerHostChildControlFailure> errorWriteResult =
                control_.WriteError(stopSequence_, shutdownFailure.message);
            if (errorWriteResult.Failed())
            {
                failure_ = errorWriteResult.Error();
                result_.store(WorldServerHostChildControlWorkerResult::TerminalWriteFailed, std::memory_order_release);
            }
            else
            {
                failure_ = std::move(shutdownFailure);
                result_.store(WorldServerHostChildControlWorkerResult::ShutdownFailed, std::memory_order_release);
            }
            return;
        }

        WorldResult<void, WorldServerHostChildControlFailure> stoppedResult = control_.WriteStopped(stopSequence_);
        if (stoppedResult.Failed())
        {
            failure_ = stoppedResult.Error();
            result_.store(WorldServerHostChildControlWorkerResult::TerminalWriteFailed, std::memory_order_release);
            return;
        }

        result_.store(WorldServerHostChildControlWorkerResult::Completed, std::memory_order_release);
    }

    void WorldServerHostChildControlWorker::CancelPendingRead() noexcept
    {
        while (reading_.load(std::memory_order_acquire) && thread_.joinable())
        {
            // 동기 IO 실행 중인 thread 취소
            if (CancelSynchronousIo(thread_.native_handle()) != FALSE)
            {
                return;
            }

            const DWORD nativeErrorCode = GetLastError();
            if (nativeErrorCode != ERROR_NOT_FOUND)
            {
                return;
            }
            // 취소할 pending sync IO 발견을 못한 경우에는
            // sync IO worker 를 위해 양보
            SwitchToThread();
        }
    }
} // namespace psnr::world::host
