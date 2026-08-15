#include "pch.h"

#include "NrSessionActorRegistry.h"

#include "NrActorScheduleGate.h"
#include "NrErrorCode.h"

#include <new>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrMutex;
    using psnr::core::NrScopedLock;

    enum class NrSessionActorLifecycle
    {
        Active,                          // 정상적으로 이벤트를 처리하는 session actor
        CloseRequestedAwaitingDrain,     // close 요청 후 pending IO 또는 active lease 정리를 기다리는 상태
        CloseRequestedReadyToDeregister, // pending IO가 없어 registry 제거를 시도할 수 있는 상태
        Deregistering,                   // registry에서 제거되는 중인 상태
    };

    struct NrSessionActorEntry final
    {
        explicit NrSessionActorEntry(std::unique_ptr<NrSessionIoActor> actorValue) noexcept
            : actor(std::move(actorValue))
        {
        }

        // ActorRun 결과 기반 entry lifecycle을 전이
        [[nodiscard]] NrSessionActorPostRunActions ApplyDrainReport(const NrSessionIoDrainReport& drainReport) noexcept;

        std::unique_ptr<NrSessionIoActor> actor;
        NrSessionActorLifecycle lifecycle = NrSessionActorLifecycle::Active;
        std::size_t activeLeaseCount = 0;
    };

    NrSessionActorPostRunActions NrSessionActorEntry::ApplyDrainReport(
        const NrSessionIoDrainReport& drainReport) noexcept
    {
        assert(lifecycle != NrSessionActorLifecycle::Deregistering);

        NrSessionActorPostRunActions actions;

        if (drainReport.closeRequested && lifecycle == NrSessionActorLifecycle::Active)
        {
            // Active 상태에서 close 요청이 들어옴 -> 현재 session 을 제거 대상으로 표시
            actions.shouldTrackClosedActorKey = true;
            lifecycle = drainReport.closeReady ? NrSessionActorLifecycle::CloseRequestedReadyToDeregister
                                               : NrSessionActorLifecycle::CloseRequestedAwaitingDrain;
        }

        if (lifecycle == NrSessionActorLifecycle::CloseRequestedAwaitingDrain && drainReport.closeReady)
        {
            lifecycle = NrSessionActorLifecycle::CloseRequestedReadyToDeregister;
        }

        if (drainReport.requestPostRecv && !drainReport.closeRequested && lifecycle == NrSessionActorLifecycle::Active)
        {
            actions.postRecvRequested = true;
        }

        actions.pendingSendQueueFull = drainReport.pendingSendQueueFull;
        actions.shouldTryDeregister = lifecycle == NrSessionActorLifecycle::CloseRequestedReadyToDeregister;
        return actions;
    }

    NrSessionActorLease::NrSessionActorLease(NrSessionActorRegistry& registry, NrSessionActorEntry& entry,
                                             NrSessionKey sessionKey) noexcept
        : registry_(&registry)
        , entry_(&entry)
        , sessionKey_(sessionKey)
    {
    }

    NrSessionActorLease::NrSessionActorLease(NrSessionActorLease&& other) noexcept
        : registry_(other.registry_)
        , entry_(other.entry_)
        , sessionKey_(other.sessionKey_)
        , registryCleanupActionsAfterRelease_(other.registryCleanupActionsAfterRelease_)
    {
        other.registry_ = nullptr;
        other.entry_ = nullptr;
        other.sessionKey_ = 0;
        other.registryCleanupActionsAfterRelease_ = {};
    }

    NrSessionActorLease& NrSessionActorLease::operator=(NrSessionActorLease&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Reset();

        registry_ = other.registry_;
        entry_ = other.entry_;
        sessionKey_ = other.sessionKey_;
        registryCleanupActionsAfterRelease_ = other.registryCleanupActionsAfterRelease_;

        other.registry_ = nullptr;
        other.entry_ = nullptr;
        other.sessionKey_ = 0;
        other.registryCleanupActionsAfterRelease_ = {};

        return *this;
    }

    NrSessionActorLease::~NrSessionActorLease() noexcept
    {
        Reset();
    }

    bool NrSessionActorLease::IsValid() const noexcept
    {
        return registry_ != nullptr && entry_ != nullptr && sessionKey_ != 0;
    }

    NrSessionKey NrSessionActorLease::SessionKey() const noexcept
    {
        return sessionKey_;
    }

    psnr::core::NrActorScheduleGate* NrSessionActorLease::ScheduleGate() const noexcept
    {
        if (!IsValid() || entry_->actor == nullptr)
        {
            return nullptr;
        }

        return entry_->actor->ScheduleGate();
    }

    NrStatus NrSessionActorLease::TryEnqueue(NrSessionRecvEvent event) noexcept
    {
        psnr::core::NrActorScheduleGate* scheduleGate = ScheduleGate();
        if (scheduleGate == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return entry_->actor->MailboxHandle(*scheduleGate).TryEnqueue(std::move(event));
    }

    NrStatus NrSessionActorLease::TryEnqueue(NrSessionSendEvent event) noexcept
    {
        if (!IsValid() || entry_->actor == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return entry_->actor->SendMailboxHandle().TryEnqueue(std::move(event));
    }

    NrActorRunReport NrSessionActorLease::TryRun(NrActorExecutor& executor) noexcept
    {
        if (!IsValid() || entry_->actor == nullptr)
        {
            return NrActorRunReport::DrainFailed(NrStatus::Failure(NrErrorCode::InvalidState),
                                                 psnr::core::NrActorScheduleDirective::NoAction);
        }

        psnr::core::NrActorScheduleGate* scheduleGate = entry_->actor->ScheduleGate();
        if (scheduleGate == nullptr)
        {
            return NrActorRunReport::DrainFailed(NrStatus::Failure(NrErrorCode::InvalidState),
                                                 psnr::core::NrActorScheduleDirective::NoAction);
        }

        NrActorRunReport runReport = executor.TryRun(*entry_->actor, *scheduleGate);

        NrSessionIoDrainReport drainReport = entry_->actor->TakeDrainReport();
        if (runReport.status.Failed())
        {
            drainReport.closeRequested = true;
            drainReport.closeReady = !entry_->actor->HasPendingIo();
            drainReport.requestPostRecv = false;
        }

        NrSessionActorPostRunActions actions = entry_->ApplyDrainReport(drainReport);
        AccumulateRegistryCleanupActions(actions);

        if (runReport.status.Failed())
        {
            return runReport;
        }

        const NrStatus postRecvStatus = TryPostRequestedRecv(actions);
        if (postRecvStatus.Failed())
        {
            runReport.status = postRecvStatus;
        }

        return runReport;
    }

    NrStatus NrSessionActorLease::TryPostRequestedRecv(const NrSessionActorPostRunActions& actions) noexcept
    {
        if (!actions.postRecvRequested)
        {
            return NrStatus::Success();
        }

        if (!IsValid() || entry_->actor == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus postRecvStatus = entry_->actor->PostRequestedRecv();
        if (postRecvStatus.Succeeded())
        {
            return NrStatus::Success();
        }

        NrSessionIoDrainReport postRecvFailureReport = entry_->actor->TakeDrainReport();
        postRecvFailureReport.closeRequested = true;
        postRecvFailureReport.closeReady = !entry_->actor->HasPendingIo();

        NrSessionActorPostRunActions postRecvFailureActions = entry_->ApplyDrainReport(postRecvFailureReport);
        AccumulateRegistryCleanupActions(postRecvFailureActions);

        return postRecvStatus;
    }

    const NrSessionActorRegistryCleanupActions& NrSessionActorLease::RegistryCleanupActionsAfterRelease() const noexcept
    {
        return registryCleanupActionsAfterRelease_;
    }

    void NrSessionActorLease::AccumulateRegistryCleanupActions(const NrSessionActorPostRunActions& actions) noexcept
    {
        registryCleanupActionsAfterRelease_.shouldTryDeregister =
            registryCleanupActionsAfterRelease_.shouldTryDeregister || actions.shouldTryDeregister;
        registryCleanupActionsAfterRelease_.shouldTrackClosedActorKey =
            registryCleanupActionsAfterRelease_.shouldTrackClosedActorKey || actions.shouldTrackClosedActorKey;
        registryCleanupActionsAfterRelease_.pendingSendQueueFull =
            registryCleanupActionsAfterRelease_.pendingSendQueueFull || actions.pendingSendQueueFull;
    }

    void NrSessionActorLease::Reset() noexcept
    {
        if (registry_ != nullptr && entry_ != nullptr)
        {
            registry_->ReleaseLease(sessionKey_, *entry_);
        }

        registry_ = nullptr;
        entry_ = nullptr;
        sessionKey_ = 0;
        registryCleanupActionsAfterRelease_ = {};
    }

    NrSessionActorRegistry::NrSessionActorRegistry(const std::size_t maxSessionCount) noexcept
        : maxSessionCount_(maxSessionCount)
    {
    }

    NrSessionActorRegistry::~NrSessionActorRegistry() noexcept = default;

    NrStatus NrSessionActorRegistry::Configure(NrBootstrapContext& context) noexcept
    {
        static_cast<void>(context);
        return NrStatus::Success();
    }

    NrStatus NrSessionActorRegistry::Start() noexcept
    {
        return NrStatus::Success();
    }

    NrStatus NrSessionActorRegistry::RequestStop(const NrStopContext& context) noexcept
    {
        static_cast<void>(context);
        return NrStatus::Success();
    }

    NrStatus NrSessionActorRegistry::Shutdown() noexcept
    {
        NrScopedLock<NrMutex> guard(lock_);

        // Server graph의 역순 shutdown에서 pipeline 정지와 scheduler join 이후 호출된다.
        // 따라서 worker와 actor state가 경쟁하지 않는 상태에서 최종 종료 사유를 publication할 수 있다.
        for (NrSessionActorMap::value_type& actorEntry : actors_)
        {
            if (actorEntry.second != nullptr && actorEntry.second->actor != nullptr)
            {
                actorEntry.second->actor->RecordServerStopping();
            }
        }

        closedActorKeys_.clear();
        actors_.clear();
        return NrStatus::Success();
    }

    NrStatus NrSessionActorRegistry::TryRegisterActor(NrSessionKey sessionKey,
                                                      std::unique_ptr<NrSessionIoActor> actor) noexcept
    {
        if (sessionKey == 0 || actor == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        std::unique_ptr<NrSessionActorEntry> entry(new (std::nothrow) NrSessionActorEntry(std::move(actor)));
        if (entry == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        NrScopedLock<NrMutex> guard(lock_);
        if (actors_.find(sessionKey) != actors_.end())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }
        if (actors_.size() >= maxSessionCount_)
        {
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        try
        {
            actors_.emplace(sessionKey, std::move(entry));
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }

    NrResult<NrSessionActorLease> NrSessionActorRegistry::TryAcquireLease(NrSessionKey sessionKey) noexcept
    {
        if (sessionKey == 0)
        {
            return NrResult<NrSessionActorLease>::Failure(NrErrorCode::InvalidArgument);
        }

        NrScopedLock<NrMutex> guard(lock_);

        NrSessionActorMap::iterator iterator = actors_.find(sessionKey);
        if (iterator == actors_.end())
        {
            return NrResult<NrSessionActorLease>::Failure(NrErrorCode::InvalidArgument);
        }

        NrSessionActorEntry& entry = *iterator->second;
        if (entry.lifecycle == NrSessionActorLifecycle::Deregistering)
        {
            return NrResult<NrSessionActorLease>::Failure(NrErrorCode::InvalidState);
        }

        ++entry.activeLeaseCount;
        return NrResult<NrSessionActorLease>(NrSessionActorLease(*this, entry, sessionKey));
    }

    NrStatus NrSessionActorRegistry::RequestClose(NrSessionKey sessionKey) noexcept
    {
        if (sessionKey == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrScopedLock<NrMutex> guard(lock_);

        NrSessionActorMap::iterator iterator = actors_.find(sessionKey);
        if (iterator == actors_.end())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrSessionActorEntry& entry = *iterator->second;
        if (entry.lifecycle == NrSessionActorLifecycle::Deregistering)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (entry.lifecycle == NrSessionActorLifecycle::Active)
        {
            try
            {
                closedActorKeys_.insert(sessionKey);
            }
            catch (const std::bad_alloc&)
            {
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            entry.lifecycle = entry.activeLeaseCount == 0 && entry.actor != nullptr && !entry.actor->HasPendingIo()
                                  ? NrSessionActorLifecycle::CloseRequestedReadyToDeregister
                                  : NrSessionActorLifecycle::CloseRequestedAwaitingDrain;
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionActorRegistry::TrackClosedActorKey(NrSessionKey sessionKey) noexcept
    {
        if (sessionKey == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrScopedLock<NrMutex> guard(lock_);

        NrSessionActorMap::iterator iterator = actors_.find(sessionKey);
        if (iterator == actors_.end() || iterator->second->lifecycle == NrSessionActorLifecycle::Deregistering)
        {
            return NrStatus::Success();
        }

        try
        {
            closedActorKeys_.insert(sessionKey);
        }
        catch (const std::bad_alloc&)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        return NrStatus::Success();
    }

    NrStatus NrSessionActorRegistry::TryDeregisterClosedActor(NrSessionKey sessionKey) noexcept
    {
        if (sessionKey == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrScopedLock<NrMutex> guard(lock_);

        NrSessionActorMap::iterator actorIterator = actors_.find(sessionKey);
        if (actorIterator == actors_.end())
        {
            closedActorKeys_.erase(sessionKey);
            return NrStatus::Success();
        }

        NrSessionActorEntry& entry = *actorIterator->second;
        if (!CanEraseLocked(entry))
        {
            return NrStatus::Success();
        }

        entry.lifecycle = NrSessionActorLifecycle::Deregistering;
        actors_.erase(actorIterator);
        closedActorKeys_.erase(sessionKey);
        return NrStatus::Success();
    }

    std::size_t NrSessionActorRegistry::DeregisterClosedActors() noexcept
    {
        std::size_t erasedCount = 0;

        NrScopedLock<NrMutex> guard(lock_);
        NrSessionActorKeySet::iterator candidateIterator = closedActorKeys_.begin();
        while (candidateIterator != closedActorKeys_.end())
        {
            const NrSessionKey sessionKey = *candidateIterator;
            NrSessionActorMap::iterator actorIterator = actors_.find(sessionKey);
            if (actorIterator == actors_.end())
            {
                candidateIterator = closedActorKeys_.erase(candidateIterator);
                continue;
            }

            NrSessionActorEntry& entry = *actorIterator->second;
            if (!CanEraseLocked(entry))
            {
                ++candidateIterator;
                continue;
            }

            entry.lifecycle = NrSessionActorLifecycle::Deregistering;
            actors_.erase(actorIterator);
            candidateIterator = closedActorKeys_.erase(candidateIterator);
            ++erasedCount;
        }

        return erasedCount;
    }

    std::size_t NrSessionActorRegistry::Count() const noexcept
    {
        NrScopedLock<NrMutex> guard(lock_);
        return actors_.size();
    }

    NrSessionActorRegistryStats NrSessionActorRegistry::Stats() const noexcept
    {
        NrScopedLock<NrMutex> guard(lock_);

        NrSessionActorRegistryStats stats;
        stats.registeredSessionCount = actors_.size();
        for (const NrSessionActorMap::value_type& actorEntry : actors_)
        {
            if (actorEntry.second->lifecycle != NrSessionActorLifecycle::Active)
            {
                ++stats.closingSessionCount;
            }
        }
        return stats;
    }

    void NrSessionActorRegistry::ReleaseLease(NrSessionKey sessionKey, NrSessionActorEntry& entry) noexcept
    {
        NrScopedLock<NrMutex> guard(lock_);

        NrSessionActorMap::iterator iterator = actors_.find(sessionKey);
        if (iterator == actors_.end() || iterator->second.get() != &entry || entry.activeLeaseCount == 0)
        {
            return;
        }

        --entry.activeLeaseCount;
    }

    bool NrSessionActorRegistry::CanEraseLocked(const NrSessionActorEntry& entry) const noexcept
    {
        if (entry.lifecycle == NrSessionActorLifecycle::Active)
            return false;

        if (entry.activeLeaseCount != 0)
            return false;

        if (entry.actor != nullptr && entry.actor->HasPendingIo())
            return false;

        return true;
    }
} // namespace psnr::runtime
