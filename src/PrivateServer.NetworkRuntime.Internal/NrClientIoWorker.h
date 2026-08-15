#pragma once

#include "NrClientIoCompletionDispatcher.h"
#include "NrIocpCompletionHandler.h"
#include "NrIocpCompletionPump.h"
#include "NrStatus.h"

#include <atomic>
#include <thread>

namespace psnr::runtime
{
    class NrIocpPort;
}

namespace psnr::runtime::internal
{
    class NrClientIoWorker final : private INrIocpCompletionHandler
    {
    public:
        NrClientIoWorker(NrIocpPort& iocpPort, INrClientIoCompletionTarget& completionTarget) noexcept;

        NrClientIoWorker(const NrClientIoWorker&) = delete;
        NrClientIoWorker& operator=(const NrClientIoWorker&) = delete;

        NrClientIoWorker(NrClientIoWorker&&) = delete;
        NrClientIoWorker& operator=(NrClientIoWorker&&) = delete;

        ~NrClientIoWorker() noexcept;

        [[nodiscard]] psnr::core::NrStatus Start() noexcept;
        [[nodiscard]] psnr::core::NrStatus Stop() noexcept;
        [[nodiscard]] psnr::core::NrStatus Join() noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        [[nodiscard]] psnr::core::NrStatus HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleControlCompletion(
            const NrIocpCompletionPacket& packet) noexcept override;

        void WorkerLoop() noexcept;

        NrIocpPort& iocpPort_;
        NrClientIoCompletionDispatcher completionDispatcher_;
        NrIocpCompletionPump completionPump_;
        std::thread workerThread_;
        std::atomic_bool workerRunning_{false};
        std::atomic_bool stopRequested_{false};
        psnr::core::NrStatus exitStatus_;
    };
} // namespace psnr::runtime::internal
