#include "pch.h"

#include "NrSessionActorScheduler.h"

#include "NrErrorCode.h"
#include "NrMemoryMath.h"
#include "NrServerMetrics.h"

#include <cassert>
#include <new>
#include <system_error>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrActorAdmissionResolution;
    using psnr::core::NrActorRunState;
    using psnr::core::NrErrorCode;
    using psnr::core::NrMemoryPoolRole;
    using psnr::core::NrResult;
    using psnr::core::NrScopedLock;
    using psnr::core::NrSessionSendEventType;
    using psnr::core::NrWaitLock;

    struct NrSessionActorScheduler::NrWorkerState final
    {
        explicit NrWorkerState(std::size_t maxAdmittedRunnableActors) noexcept
            : admittedRunnableActors(maxAdmittedRunnableActors)
        {
        }

        std::unique_ptr<NrReadyQueue> readyQueue; // Bounded MPSC Queue
        NrRunnableActorPermitCounter admittedRunnableActors;
        NrMutex waitLock;
        std::condition_variable_any wakeup;
        std::thread workerThread;
    };

    NrActorScheduleHandle::NrActorScheduleHandle(NrSessionActorScheduler& scheduler) noexcept
        : scheduler_(&scheduler)
    {
    }

    bool NrActorScheduleHandle::IsValid() const noexcept
    {
        return scheduler_ != nullptr;
    }

    NrStatus NrActorScheduleHandle::Enqueue(NrSessionKey sessionKey, NrSessionRecvEvent event) const noexcept
    {
        if (scheduler_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return scheduler_->Enqueue(sessionKey, std::move(event));
    }

    NrStatus NrActorScheduleHandle::Enqueue(NrSessionKey sessionKey, NrSessionSendEvent event) const noexcept
    {
        if (scheduler_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return scheduler_->Enqueue(sessionKey, std::move(event));
    }

    NrSessionActorScheduler::NrSessionActorScheduler(NrSessionActorRegistry& sessionActorRegistry,
                                                     NrMemoryPoolManager& memoryPoolManager,
                                                     internal::NrServerMetrics& metrics,
                                                     NrSessionActorSchedulerConfig config,
                                                     internal::NrDiagnosticEmitter diagnosticsEmitter) noexcept
        : sessionActorRegistry_(&sessionActorRegistry)
        , memoryPoolManager_(&memoryPoolManager)
        , metrics_(&metrics)
        , diagnosticsEmitter_(diagnosticsEmitter)
        , config_(config)
        , actorExecutor_(config.actorDrainBudget)
    {
    }

    NrSessionActorScheduler::~NrSessionActorScheduler() noexcept
    {
        static_cast<void>(Shutdown());
    }

    NrActorScheduleHandle NrSessionActorScheduler::ScheduleHandle() noexcept
    {
        return NrActorScheduleHandle(*this);
    }

    NrStatus NrSessionActorScheduler::Enqueue(NrSessionKey sessionKey, NrSessionRecvEvent event) noexcept
    {
        if (sessionKey == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!started_.load() || stopRequested_.load())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }
        assert(sessionActorRegistry_ != nullptr);

        NrResult<NrSessionActorLease> leaseResult = sessionActorRegistry_->TryAcquireLease(sessionKey);
        if (leaseResult.Failed())
        {
            return leaseResult.Status();
        }

        NrSessionActorLease& lease = leaseResult.Value();
        NrActorScheduleGate* scheduleGate = lease.ScheduleGate();
        if (scheduleGate == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrActorAdmissionTicket ticket = BeginAdmission(*scheduleGate);
        if (!ticket.Accepted())
        {
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        if (ticket.NeedsNewPermit() && !TryAcquireRunnablePermit(ShardIndex(sessionKey)))
        {
            const NrActorScheduleDirective directive =
                scheduleGate->CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::CapacityRejected);
            assert(directive == NrActorScheduleDirective::NoAction);
            metrics_->Record(NrPressureTransactionOutcome::ActorAdmissionReadyCapacityRejected);
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        const NrStatus enqueueStatus = lease.TryEnqueue(std::move(event));
        const NrActorAdmissionResolution resolution = enqueueStatus.Succeeded()
                                                          ? NrActorAdmissionResolution::MailboxCommitted
                                                          : NrActorAdmissionResolution::MailboxRejected;
        const NrActorScheduleDirective directive = scheduleGate->CompleteAdmission(std::move(ticket), resolution);
        if (enqueueStatus.ErrorCode() == NrErrorCode::QueueFull)
        {
            metrics_->Record(NrPressureTransactionOutcome::ActorAdmissionMailboxRejected);
        }
        const NrStatus directiveStatus = ApplyScheduleDirective(sessionKey, directive);
        return directiveStatus.Failed() ? directiveStatus : enqueueStatus;
    }

    NrStatus NrSessionActorScheduler::Enqueue(NrSessionKey sessionKey, NrSessionSendEvent event) noexcept
    {
        if (sessionKey == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!started_.load() || stopRequested_.load())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }
        assert(sessionActorRegistry_ != nullptr);

        NrResult<NrSessionActorLease> leaseResult = sessionActorRegistry_->TryAcquireLease(sessionKey);
        if (leaseResult.Failed())
        {
            return leaseResult.Status();
        }

        NrSessionActorLease& lease = leaseResult.Value();
        NrActorScheduleGate* scheduleGate = lease.ScheduleGate();
        if (scheduleGate == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrActorAdmissionTicket ticket = BeginAdmission(*scheduleGate);
        if (!ticket.Accepted())
        {
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        if (ticket.NeedsNewPermit() && !TryAcquireRunnablePermit(ShardIndex(sessionKey)))
        {
            const NrActorScheduleDirective directive =
                scheduleGate->CompleteAdmission(std::move(ticket), NrActorAdmissionResolution::CapacityRejected);
            assert(directive == NrActorScheduleDirective::NoAction);
            metrics_->Record(NrPressureTransactionOutcome::ActorAdmissionReadyCapacityRejected);
            return NrStatus::Failure(NrErrorCode::QueueFull);
        }

        const bool tracksSendBacklog = event.type == NrSessionSendEventType::SendRequested;
        const std::uint64_t admittedSendMailboxDepth = tracksSendBacklog ? metrics_->BeginSendMailboxEnqueue() : 0;
        const NrStatus enqueueStatus = lease.TryEnqueue(std::move(event));
        if (tracksSendBacklog)
        {
            if (enqueueStatus.Succeeded())
            {
                metrics_->CommitSendMailboxEnqueue(admittedSendMailboxDepth);
            }
            else
            {
                metrics_->CancelSendMailboxEnqueue();
            }
        }
        const NrActorAdmissionResolution resolution = enqueueStatus.Succeeded()
                                                          ? NrActorAdmissionResolution::MailboxCommitted
                                                          : NrActorAdmissionResolution::MailboxRejected;
        const NrActorScheduleDirective directive = scheduleGate->CompleteAdmission(std::move(ticket), resolution);
        if (enqueueStatus.ErrorCode() == NrErrorCode::QueueFull)
        {
            metrics_->Record(NrPressureTransactionOutcome::SendAdmissionRejected);
        }
        const NrStatus directiveStatus = ApplyScheduleDirective(sessionKey, directive);
        return directiveStatus.Failed() ? directiveStatus : enqueueStatus;
    }

    std::size_t NrSessionActorScheduler::ReadyQueueSize() const noexcept
    {
        std::size_t size = 0;
        for (const std::unique_ptr<NrWorkerState>& worker : workers_)
        {
            if (worker != nullptr && worker->readyQueue != nullptr)
            {
                size += worker->readyQueue->SizeApprox();
            }
        }

        return size;
    }

    std::size_t NrSessionActorScheduler::AdmittedRunnableCount() const noexcept
    {
        std::size_t count = 0;
        for (const std::unique_ptr<NrWorkerState>& worker : workers_)
        {
            if (worker != nullptr)
            {
                count += worker->admittedRunnableActors.Count();
            }
        }

        return count;
    }

    NrStatus NrSessionActorScheduler::Configure(NrBootstrapContext& context) noexcept
    {
        static_cast<void>(context);

        NrStatus validationStatus = ValidateConfig();
        if (validationStatus.Failed())
        {
            return validationStatus;
        }

        NrStatus storageStatus = EnsureReadyQueueStorage();
        if (storageStatus.Failed())
        {
            return storageStatus;
        }

        NrScopedLock<NrMutex> guard(lifecycleLock_);
        if (started_.load()) // already started
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        configured_ = true;
        stopRequested_.store(false);
        return NrStatus::Success();
    }

    NrStatus NrSessionActorScheduler::Start() noexcept
    {
        {
            NrScopedLock<NrMutex> guard(lifecycleLock_);
            if (!configured_ || started_.load())
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            started_.store(true);
            stopRequested_.store(false);
        }

        try
        {
            for (std::size_t index = 0; index < workers_.size(); ++index)
            {
                workers_[index]->workerThread = std::thread(&NrSessionActorScheduler::WorkerLoop, this, index);
            }
        }
        catch (const std::bad_alloc&)
        {
            static_cast<void>(Shutdown());
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }
        catch (const std::system_error&)
        {
            static_cast<void>(Shutdown());
            return NrStatus::Failure(NrErrorCode::IoFailed);
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionActorScheduler::RequestStop(const NrStopContext& context) noexcept
    {
        static_cast<void>(context);

        stopRequested_.store(true);
        {
            NrScopedLock<NrMutex> guard(lifecycleLock_);
            started_.store(false);
        }
        WakeAllWorkers();
        return NrStatus::Success();
    }

    NrStatus NrSessionActorScheduler::Shutdown() noexcept
    {
        stopRequested_.store(true);
        {
            NrScopedLock<NrMutex> guard(lifecycleLock_);
            started_.store(false);
        }
        WakeAllWorkers();

        for (std::unique_ptr<NrWorkerState>& worker : workers_)
        {
            if (worker != nullptr && worker->workerThread.joinable())
            {
                worker->workerThread.join();
            }
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionActorScheduler::ValidateConfig() const noexcept
    {
        assert(sessionActorRegistry_ != nullptr);
        assert(memoryPoolManager_ != nullptr);
        assert(metrics_ != nullptr);

        if (config_.actorWorkerCount == 0 || config_.actorDrainBudget.maxEvents == 0 ||
            config_.maxAdmittedRunnableActors == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!psnr::core::utils::NrIsPowerOfTwo(config_.maxAdmittedRunnableActors))
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionActorScheduler::EnsureReadyQueueStorage() noexcept
    {
        if (memoryPoolManager_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        try
        {
            std::vector<std::unique_ptr<NrWorkerState>> workers;
            workers.reserve(config_.actorWorkerCount);

            for (std::size_t index = 0; index < config_.actorWorkerCount; ++index)
            {
                std::unique_ptr<NrWorkerState> worker(new (std::nothrow)
                                                          NrWorkerState(config_.maxAdmittedRunnableActors));
                if (worker == nullptr)
                {
                    return NrStatus::Failure(NrErrorCode::OutOfMemory);
                }

                NrResult<std::unique_ptr<NrReadyQueue>> readyQueueResult = NrReadyQueue::Create(
                    *memoryPoolManager_, NrMemoryPoolRole::ActorReadyQueueStorage, config_.maxAdmittedRunnableActors);
                if (readyQueueResult.Failed())
                {
                    return readyQueueResult.Status();
                }

                worker->readyQueue = readyQueueResult.TakeValue();
                workers.push_back(std::move(worker));
            }

            workers_ = std::move(workers);
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }

    std::size_t NrSessionActorScheduler::ShardIndex(NrSessionKey sessionKey) const noexcept
    {
        return workers_.empty() ? 0 : static_cast<std::size_t>(sessionKey % workers_.size());
    }

    bool NrSessionActorScheduler::TryAcquireRunnablePermit(std::size_t workerIndex) noexcept
    {
        return workerIndex < workers_.size() && workers_[workerIndex] != nullptr &&
               workers_[workerIndex]->admittedRunnableActors.TryAcquire();
    }

    void NrSessionActorScheduler::ReleaseRunnablePermit(std::size_t workerIndex) noexcept
    {
        if (workerIndex >= workers_.size() || workers_[workerIndex] == nullptr)
        {
            assert(false && "missing worker for runnable actor permit release");
            return;
        }

        workers_[workerIndex]->admittedRunnableActors.Release();
    }

    NrActorAdmissionTicket NrSessionActorScheduler::BeginAdmission(NrActorScheduleGate& scheduleGate) const noexcept
    {
        while (started_.load(std::memory_order_acquire) && !stopRequested_.load(std::memory_order_acquire))
        {
            NrActorAdmissionTicket ticket = scheduleGate.TryBeginAdmission();
            if (ticket.Accepted())
            {
                return ticket;
            }

            // 먼저 admission transaction 을 시도한 producer 의 최종 결과를 기다리기 위해 yield 하고 다시 시도
            std::this_thread::yield();
        }

        return NrActorAdmissionTicket::Rejected();
    }

    NrStatus NrSessionActorScheduler::ApplyScheduleDirective(NrSessionKey sessionKey,
                                                             NrActorScheduleDirective directive) noexcept
    {
        switch (directive)
        {
        case NrActorScheduleDirective::NoAction:
        case NrActorScheduleDirective::FinalizationDeferred:
            return NrStatus::Success();

        case NrActorScheduleDirective::EnqueueReadyToken:
        {
            const NrStatus pushStatus = TryPushReadyQueue(sessionKey);
            if (pushStatus.Failed())
            {
                // BeginAdmission 호출 이후 RunnablePermit이 확보된 상태에서만 mailbox commit 을 진행함
                // 따라서 ready qeueu 자리를 미리 예약한 상태인데, enqueue ready queue가 실패했으면 비정상인 상황
                EmitAdmissionAnomaly(sessionKey, pushStatus);
            }
            assert(pushStatus.Succeeded() && "a reserved runnable permit must guarantee ready queue capacity");
            return pushStatus;
        }

        case NrActorScheduleDirective::ReleasePermit:
            ReleaseRunnablePermit(ShardIndex(sessionKey));
            return NrStatus::Success();
        }

        assert(false && "unknown actor schedule directive");
        return NrStatus::Failure(NrErrorCode::InvalidState);
    }

    void NrSessionActorScheduler::EmitAdmissionAnomaly(const NrSessionKey sessionKey,
                                                       const NrStatus& status) const noexcept
    {
        assert(sessionKey != 0);
        assert(status.Failed());

        internal::NrDiagnosticRecord record;
        record.sessionKey = sessionKey;
        record.component = internal::NrDiagnosticComponent::ActorScheduler;
        record.operation = internal::NrDiagnosticOperation::Admission;
        record.severity = internal::NrDiagnosticSeverity::Error;
        record.eventKind = internal::NrDiagnosticEventKind::Anomaly;
        record.errorCode = status.ErrorCode();
        record.nativeErrorCode = status.NativeErrorCode();
        record.contextFlags = internal::NrDiagnosticContextFlags::HasSessionKey;
        diagnosticsEmitter_.Emit(record);
    }

    NrStatus NrSessionActorScheduler::TryPushReadyQueue(NrSessionKey sessionKey) noexcept
    {
        const std::size_t workerIndex = ShardIndex(sessionKey);
        if (workerIndex >= workers_.size() || workers_[workerIndex] == nullptr ||
            workers_[workerIndex]->readyQueue == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus pushStatus = workers_[workerIndex]->readyQueue->TryPush(sessionKey);
        if (pushStatus.Failed())
        {
            return pushStatus;
        }

        WakeWorker(workerIndex);
        return NrStatus::Success();
    }

    bool NrSessionActorScheduler::TryPopReadyQueue(std::size_t workerIndex, NrSessionKey& outSessionKey) noexcept
    {
        if (workerIndex >= workers_.size() || workers_[workerIndex] == nullptr ||
            workers_[workerIndex]->readyQueue == nullptr)
        {
            return false;
        }

        const NrStatus popStatus = workers_[workerIndex]->readyQueue->TryPop(outSessionKey);
        return popStatus.Succeeded();
    }

    void NrSessionActorScheduler::WakeWorker(std::size_t workerIndex) noexcept
    {
        if (workerIndex < workers_.size() && workers_[workerIndex] != nullptr)
        {
            NrWorkerState& worker = *workers_[workerIndex];
            {
                // Queue/stop state is already published. This handshake prevents notify from
                // racing between the worker's false predicate check and its actual wait.
                NrScopedLock<NrMutex> guard(worker.waitLock);
            }
            worker.wakeup.notify_one();
        }
    }

    void NrSessionActorScheduler::WakeAllWorkers() noexcept
    {
        for (std::size_t index = 0; index < workers_.size(); ++index)
        {
            WakeWorker(index);
        }
    }

    void NrSessionActorScheduler::WorkerLoop(std::size_t workerIndex) noexcept // actor thread loop
    {
        if (workerIndex >= workers_.size() || workers_[workerIndex] == nullptr)
        {
            return;
        }

        NrWorkerState& worker = *workers_[workerIndex];
        while (true)
        {
            NrSessionKey sessionKey = 0;
            if (!TryPopReadyQueue(workerIndex, sessionKey))
            {
                NrWaitLock lock(worker.waitLock);
                worker.wakeup.wait(lock,
                                   [this, &worker]() noexcept
                                   {
                                       return stopRequested_.load() ||
                                              (worker.readyQueue != nullptr && worker.readyQueue->SizeApprox() > 0);
                                   });

                if (stopRequested_.load() && (worker.readyQueue == nullptr || worker.readyQueue->SizeApprox() == 0))
                {
                    return;
                }

                if (!TryPopReadyQueue(workerIndex, sessionKey)) // wait 후 sessionKey re-check
                {
                    continue;
                }
            }

            RunReadyActor(sessionKey);
        }
    }

    void NrSessionActorScheduler::RunReadyActor(NrSessionKey sessionKey) noexcept
    {
        assert(sessionActorRegistry_ != nullptr);

        NrResult<NrSessionActorLease> leaseResult = sessionActorRegistry_->TryAcquireLease(sessionKey);
        if (leaseResult.Failed())
        {
            ReleaseRunnablePermit(ShardIndex(sessionKey));
            return;
        }

        NrSessionActorLease& lease = leaseResult.Value();
        const NrActorRunReport runReport = lease.TryRun(actorExecutor_);
        const NrSessionActorRegistryCleanupActions cleanupActions = lease.RegistryCleanupActionsAfterRelease();
        const bool shouldTryDeregister = cleanupActions.shouldTryDeregister;
        bool shouldRunDeregisterCleanup = shouldTryDeregister; // fail case

        const NrActorRunCompletion completion = CompleteActorRun(sessionKey, runReport);
        shouldRunDeregisterCleanup = shouldTryDeregister && completion == NrActorRunCompletion::Finished;

        lease.Reset();
        RunRegistryCleanupAfterLeaseRelease(sessionKey, cleanupActions.shouldTrackClosedActorKey,
                                            shouldRunDeregisterCleanup);
    }

    NrSessionActorScheduler::NrActorRunCompletion NrSessionActorScheduler::CompleteActorRun(
        NrSessionKey sessionKey, const NrActorRunReport& runReport) noexcept
    {
        if (runReport.runState == NrActorRunState::AlreadyDraining)
        {
            return NrActorRunCompletion::Skipped;
        }

        switch (runReport.scheduleDirective)
        {
        case NrActorScheduleDirective::EnqueueReadyToken:
            return ApplyScheduleDirective(sessionKey, runReport.scheduleDirective).Succeeded()
                       ? NrActorRunCompletion::Rescheduled
                       : NrActorRunCompletion::Finished;

        case NrActorScheduleDirective::ReleasePermit:
            static_cast<void>(ApplyScheduleDirective(sessionKey, runReport.scheduleDirective));
            return NrActorRunCompletion::Finished;

        case NrActorScheduleDirective::FinalizationDeferred:
            return NrActorRunCompletion::Deferred;

        case NrActorScheduleDirective::NoAction:
            break;
        }

        return NrActorRunCompletion::Finished;
    }

    void NrSessionActorScheduler::RunRegistryCleanupAfterLeaseRelease(NrSessionKey sessionKey,
                                                                      bool shouldTrackClosedActorKey,
                                                                      bool shouldTryDeregister) noexcept
    {
        assert(sessionActorRegistry_ != nullptr);

        if (shouldTrackClosedActorKey)
        {
            static_cast<void>(sessionActorRegistry_->TrackClosedActorKey(sessionKey));
        }

        if (shouldTryDeregister)
        {
            static_cast<void>(sessionActorRegistry_->TryDeregisterClosedActor(sessionKey));
        }
    }

} // namespace psnr::runtime
