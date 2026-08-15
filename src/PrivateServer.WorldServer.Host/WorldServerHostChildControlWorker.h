#pragma once

#include "WorldServerHostChildControl.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace psnr::world::host
{
    class WorldServerHostStopSignal;

    enum class WorldServerHostChildControlWorkerResult : std::uint8_t
    {
        NotStarted = 0,
        Running,
        StopReceived,
        Completed,
        ReadyWriteFailed,
        ReadFailed,
        ShutdownFailed,
        TerminalWriteFailed,
        Cancelled,
        StartFailed,
    };

    class WorldServerHostChildControlWorker final
    {
    public:
        WorldServerHostChildControlWorker(WorldServerHostChildControl& control,
                                          WorldServerHostStopSignal& stopSignal) noexcept;
        ~WorldServerHostChildControlWorker();

        WorldServerHostChildControlWorker(const WorldServerHostChildControlWorker&) = delete;
        WorldServerHostChildControlWorker& operator=(const WorldServerHostChildControlWorker&) = delete;

        [[nodiscard]] bool Start() noexcept;
        void NotifyShutdownCompleted(bool succeeded) noexcept;
        void RequestStop() noexcept;
        void Join() noexcept;

        [[nodiscard]] WorldServerHostChildControlWorkerResult Result() const noexcept;
        [[nodiscard]] std::uint64_t StopSequence() const noexcept;
        [[nodiscard]] const WorldServerHostChildControlFailure& Failure() const noexcept;

    private:
        void Run() noexcept;
        void CancelPendingRead() noexcept;

        WorldServerHostChildControl& control_;
        WorldServerHostStopSignal& stopSignal_;

        std::thread thread_;
        std::atomic<bool> started_ = false;
        std::atomic<bool> stopRequested_ = false;
        std::atomic<bool> reading_ = false;
        std::atomic<WorldServerHostChildControlWorkerResult> result_ =
            WorldServerHostChildControlWorkerResult::NotStarted;

        std::mutex shutdownMutex_;
        std::condition_variable shutdownCondition_;
        bool shutdownCompleted_ = false;
        bool shutdownSucceeded_ = false;

        std::uint64_t stopSequence_ = 0;
        WorldServerHostChildControlFailure failure_{};
    };
} // namespace psnr::world::host
