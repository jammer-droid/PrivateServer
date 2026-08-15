#pragma once

#include "NrActorExecutor.h"
#include "NrActorScheduleHandle.h"
#include "NrBoundedMpscQueue.h"
#include "NrConcurrency.h"
#include "NrLifecycleInternal.h"
#include "NrMemoryPoolManager.h"
#include "NrSessionActorRegistry.h"
#include "NrSessionIoEvent.h"
#include "NrSessionKey.h"
#include "NrStatus.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace psnr::runtime
{
    namespace internal
    {
        class NrServerMetrics;
    }

    using psnr::core::NrActorAdmissionTicket;
    using psnr::core::NrActorDrainBudget;
    using psnr::core::NrActorExecutor;
    using psnr::core::NrActorRunReport;
    using psnr::core::NrActorScheduleDirective;
    using psnr::core::NrActorScheduleGate;
    using psnr::core::NrBoundedMpscQueue;
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrMutex;
    using psnr::core::NrSessionKey;
    using psnr::core::NrSessionRecvEvent;
    using psnr::core::NrSessionSendEvent;
    using psnr::core::NrStatus;

    class NrSessionActorScheduler;
    class NrSessionActorSchedulerTestAccess;

    class NrRunnableActorPermitCounter final
    {
    public:
        explicit NrRunnableActorPermitCounter(std::size_t capacity) noexcept
            : capacity_(capacity)
        {
        }

        [[nodiscard]] bool TryAcquire() noexcept
        {
            std::size_t observed = count_.load(std::memory_order_relaxed);
            while (observed < capacity_)
            {
                if (count_.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed,
                                                 std::memory_order_relaxed))
                {
                    return true;
                }
            }

            return false;
        }

        void Release() noexcept
        {
            std::size_t observed = count_.load(std::memory_order_relaxed);
            while (observed > 0)
            {
                if (count_.compare_exchange_weak(observed, observed - 1, std::memory_order_relaxed,
                                                 std::memory_order_relaxed))
                {
                    return;
                }
            }

            assert(false && "runnable actor permit underflow");
        }

        [[nodiscard]] std::size_t Count() const noexcept
        {
            return count_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] std::size_t Capacity() const noexcept
        {
            return capacity_;
        }

    private:
        // count_는 permit 예약 중 + ready queue 대기 중 + actor drain 중인 runnable actor의 합이다.
        std::size_t capacity_ = 0;
        std::atomic_size_t count_{0};
    };

    struct NrSessionActorSchedulerConfig final
    {
        std::size_t actorWorkerCount = 2;             // power of two
        NrActorDrainBudget actorDrainBudget{16};      //
        std::size_t maxAdmittedRunnableActors = 1024; //
    };

    class NrSessionActorScheduler final : public INrServerLifecycleComponent
    {
    public:
        NrSessionActorScheduler(NrSessionActorRegistry& sessionActorRegistry, NrMemoryPoolManager& memoryPoolManager,
                                internal::NrServerMetrics& metrics, NrSessionActorSchedulerConfig config = {},
                                internal::NrDiagnosticEmitter diagnosticsEmitter = {}) noexcept;

        NrSessionActorScheduler(const NrSessionActorScheduler&) = delete;
        NrSessionActorScheduler& operator=(const NrSessionActorScheduler&) = delete;

        NrSessionActorScheduler(NrSessionActorScheduler&&) = delete;
        NrSessionActorScheduler& operator=(NrSessionActorScheduler&&) = delete;

        ~NrSessionActorScheduler() noexcept override;

        [[nodiscard]] NrActorScheduleHandle ScheduleHandle() noexcept;

        [[nodiscard]] NrStatus Enqueue(NrSessionKey sessionKey, NrSessionRecvEvent event) noexcept;
        [[nodiscard]] NrStatus Enqueue(NrSessionKey sessionKey, NrSessionSendEvent event) noexcept;

        [[nodiscard]] std::size_t ReadyQueueSize() const noexcept;
        [[nodiscard]] std::size_t AdmittedRunnableCount() const noexcept;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override;
        [[nodiscard]] NrStatus Start() noexcept override;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override;
        [[nodiscard]] NrStatus Shutdown() noexcept override;

    private:
        friend class NrSessionActorSchedulerTestAccess;

        struct NrWorkerState;

        enum class NrActorRunCompletion
        {
            Finished,
            Rescheduled,
            Deferred,
            Skipped,
        };

        [[nodiscard]] NrStatus ValidateConfig() const noexcept;
        [[nodiscard]] NrStatus EnsureReadyQueueStorage() noexcept;

        [[nodiscard]] std::size_t ShardIndex(NrSessionKey sessionKey) const noexcept;
        [[nodiscard]] bool TryAcquireRunnablePermit(std::size_t workerIndex) noexcept;
        void ReleaseRunnablePermit(std::size_t workerIndex) noexcept;
        [[nodiscard]] NrStatus TryPushReadyQueue(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] bool TryPopReadyQueue(std::size_t workerIndex, NrSessionKey& outSessionKey) noexcept;
        [[nodiscard]] NrActorAdmissionTicket BeginAdmission(NrActorScheduleGate& scheduleGate) const noexcept;
        [[nodiscard]] NrStatus ApplyScheduleDirective(NrSessionKey sessionKey,
                                                      NrActorScheduleDirective directive) noexcept;
        void EmitAdmissionAnomaly(NrSessionKey sessionKey, const NrStatus& status) const noexcept;

        void WakeWorker(std::size_t workerIndex) noexcept;
        void WakeAllWorkers() noexcept;
        void WorkerLoop(std::size_t workerIndex) noexcept;

        void RunReadyActor(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] NrActorRunCompletion CompleteActorRun(NrSessionKey sessionKey,
                                                            const NrActorRunReport& runReport) noexcept;
        void RunRegistryCleanupAfterLeaseRelease(NrSessionKey sessionKey, bool shouldTrackClosedActorKey,
                                                 bool shouldTryDeregister) noexcept;

        using NrReadyQueue = NrBoundedMpscQueue<NrSessionKey>;

        NrSessionActorRegistry* sessionActorRegistry_ = nullptr; // non-owning
        NrMemoryPoolManager* memoryPoolManager_ = nullptr;       // graph-owned
        internal::NrServerMetrics* metrics_ = nullptr;           // graph-owned
        internal::NrDiagnosticEmitter diagnosticsEmitter_;
        NrSessionActorSchedulerConfig config_;
        NrActorExecutor actorExecutor_;

        mutable NrMutex lifecycleLock_;

        std::vector<std::unique_ptr<NrWorkerState>> workers_;

        bool configured_ = false;
        std::atomic_bool started_{false};
        std::atomic_bool stopRequested_{false};
    };
} // namespace psnr::runtime
