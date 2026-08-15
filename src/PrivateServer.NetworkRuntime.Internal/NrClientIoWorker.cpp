#include "pch.h"

#include "NrClientIoWorker.h"

#include "NrClientControlCompletion.h"
#include "NrErrorCode.h"
#include "NrIocpPort.h"

#include <system_error>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    NrClientIoWorker::NrClientIoWorker(NrIocpPort& iocpPort, INrClientIoCompletionTarget& completionTarget) noexcept
        : iocpPort_(iocpPort)
        , completionDispatcher_(completionTarget)
        , completionPump_(iocpPort_, *this)
    {
    }

    NrClientIoWorker::~NrClientIoWorker() noexcept
    {
        (void)Stop();
    }

    NrStatus NrClientIoWorker::Start() noexcept
    {
        if (!iocpPort_.IsValid() || workerThread_.joinable() || workerRunning_.load(std::memory_order_acquire))
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        stopRequested_.store(false, std::memory_order_release);
        exitStatus_ = NrStatus::Success();
        workerRunning_.store(true, std::memory_order_release);

        try
        {
            workerThread_ = std::thread(&NrClientIoWorker::WorkerLoop, this);
        }
        catch (const std::system_error&)
        {
            workerRunning_.store(false, std::memory_order_release);
            return NrStatus::Failure(NrErrorCode::IoFailed);
        }

        return NrStatus::Success();
    }

    NrStatus NrClientIoWorker::Stop() noexcept
    {
        if (!workerThread_.joinable())
        {
            workerRunning_.store(false, std::memory_order_release);
            stopRequested_.store(true, std::memory_order_release);
            return NrStatus::Success();
        }

        if (workerThread_.get_id() == std::this_thread::get_id())
        {
            stopRequested_.store(true, std::memory_order_release);
            return NrStatus::Success();
        }

        if (!workerRunning_.load(std::memory_order_acquire))
        {
            return Join();
        }

        const NrStatus wakeStatus = PostClientControlCompletion(iocpPort_, NrClientControlCompletionKind::Stop);
        if (wakeStatus.Failed())
        {
            return wakeStatus;
        }

        return Join();
    }

    NrStatus NrClientIoWorker::Join() noexcept
    {
        if (!workerThread_.joinable())
        {
            return exitStatus_;
        }

        if (workerThread_.get_id() == std::this_thread::get_id())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        workerThread_.join();
        workerRunning_.store(false, std::memory_order_release);
        return exitStatus_;
    }

    bool NrClientIoWorker::IsRunning() const noexcept
    {
        return workerRunning_.load(std::memory_order_acquire);
    }

    NrStatus NrClientIoWorker::HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept
    {
        return completionDispatcher_.HandleIoCompletion(packet);
    }

    NrStatus NrClientIoWorker::HandleControlCompletion(const NrIocpCompletionPacket& packet) noexcept
    {
        NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
        const NrStatus decodeStatus = DecodeClientControlCompletion(packet, kind);
        if (decodeStatus.Failed())
        {
            return decodeStatus;
        }

        if (kind == NrClientControlCompletionKind::Stop)
        {
            stopRequested_.store(true, std::memory_order_release);
            return NrStatus::Success();
        }

        return completionDispatcher_.HandleControlCompletion(packet);
    }

    void NrClientIoWorker::WorkerLoop() noexcept
    {
        while (!stopRequested_.load(std::memory_order_acquire))
        {
            const NrStatus pumpStatus = completionPump_.PumpOnce();
            if (pumpStatus.Failed() && !stopRequested_.load(std::memory_order_acquire))
            {
                exitStatus_ = pumpStatus;
                stopRequested_.store(true, std::memory_order_release);
            }
        }

        workerRunning_.store(false, std::memory_order_release);
    }
} // namespace psnr::runtime::internal
