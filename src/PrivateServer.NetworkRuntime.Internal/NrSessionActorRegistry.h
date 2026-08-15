#pragma once

#include "NrActorExecutor.h"
#include "NrConcurrency.h"
#include "NrLifecycleInternal.h"
#include "NrResult.h"
#include "NrSessionIoActor.h"
#include "NrSessionIoEvent.h"
#include "NrSessionKey.h"
#include "NrStatus.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace psnr::runtime
{
    using psnr::core::NrActorExecutor;
    using psnr::core::NrActorRunReport;
    using psnr::core::NrResult;
    using psnr::core::NrSessionIoActor;
    using psnr::core::NrSessionIoDrainReport;
    using psnr::core::NrSessionKey;
    using psnr::core::NrSessionRecvEvent;
    using psnr::core::NrSessionSendEvent;
    using psnr::core::NrStatus;

    struct NrSessionActorEntry;

    class NrSessionActorRegistry;

    struct NrSessionActorRegistryStats final
    {
        std::size_t registeredSessionCount = 0;
        std::size_t closingSessionCount = 0;
    };

    struct NrSessionActorPostRunActions final
    {
        bool shouldTryDeregister = false;       // 현재 run 결과를 기준으로 deregister 시도 가능한지
        bool shouldTrackClosedActorKey = false; // 해당 actor의 상태가 close/drain 계열로 전이, 삭제 후보 키 등록
        bool postRecvRequested = false;
        bool pendingSendQueueFull = false;
    };

    struct NrSessionActorRegistryCleanupActions final
    {
        bool shouldTryDeregister = false;
        bool shouldTrackClosedActorKey = false;
        bool pendingSendQueueFull = false;
    };

    class NrSessionActorLease final // move-only
    {
    public:
        NrSessionActorLease() noexcept = default;

        NrSessionActorLease(const NrSessionActorLease&) = delete;
        NrSessionActorLease& operator=(const NrSessionActorLease&) = delete;

        NrSessionActorLease(NrSessionActorLease&& other) noexcept;
        NrSessionActorLease& operator=(NrSessionActorLease&& other) noexcept;

        ~NrSessionActorLease() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] NrSessionKey SessionKey() const noexcept;

        [[nodiscard]] psnr::core::NrActorScheduleGate* ScheduleGate() const noexcept;
        [[nodiscard]] NrStatus TryEnqueue(NrSessionRecvEvent event) noexcept;
        [[nodiscard]] NrStatus TryEnqueue(NrSessionSendEvent event) noexcept;
        [[nodiscard]] NrActorRunReport TryRun(NrActorExecutor& executor) noexcept;
        [[nodiscard]] const NrSessionActorRegistryCleanupActions& RegistryCleanupActionsAfterRelease() const noexcept;

        void Reset() noexcept;

    private:
        friend class NrSessionActorRegistry;

        NrSessionActorLease(NrSessionActorRegistry& registry, NrSessionActorEntry& entry,
                            NrSessionKey sessionKey) noexcept;
        [[nodiscard]] NrStatus TryPostRequestedRecv(const NrSessionActorPostRunActions& actions) noexcept;
        void AccumulateRegistryCleanupActions(const NrSessionActorPostRunActions& actions) noexcept;

        NrSessionActorRegistry* registry_ = nullptr; // non-owning; registry must outlive active leases.
        NrSessionActorEntry* entry_ = nullptr;       // guarded by active lease count while valid.
        NrSessionKey sessionKey_ = 0;
        NrSessionActorRegistryCleanupActions registryCleanupActionsAfterRelease_;
    };

    class NrSessionActorRegistry final : public INrServerLifecycleComponent
    {
    public:
        explicit NrSessionActorRegistry(std::size_t maxSessionCount) noexcept;

        NrSessionActorRegistry(const NrSessionActorRegistry&) = delete;
        NrSessionActorRegistry& operator=(const NrSessionActorRegistry&) = delete;

        NrSessionActorRegistry(NrSessionActorRegistry&&) = delete;
        NrSessionActorRegistry& operator=(NrSessionActorRegistry&&) = delete;

        ~NrSessionActorRegistry() noexcept override;

        [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override;
        [[nodiscard]] NrStatus Start() noexcept override;
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override;
        [[nodiscard]] NrStatus Shutdown() noexcept override;

        [[nodiscard]] NrStatus TryRegisterActor(NrSessionKey sessionKey,
                                                std::unique_ptr<NrSessionIoActor> actor) noexcept;
        [[nodiscard]] NrResult<NrSessionActorLease> TryAcquireLease(NrSessionKey sessionKey) noexcept;

        [[nodiscard]] NrStatus RequestClose(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] NrStatus TrackClosedActorKey(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] NrStatus TryDeregisterClosedActor(NrSessionKey sessionKey) noexcept;
        [[nodiscard]] std::size_t DeregisterClosedActors() noexcept;

        [[nodiscard]] std::size_t Count() const noexcept;
        [[nodiscard]] NrSessionActorRegistryStats Stats() const noexcept;

    private:
        friend class NrSessionActorLease;

        void ReleaseLease(NrSessionKey sessionKey, NrSessionActorEntry& entry) noexcept;
        [[nodiscard]] bool CanEraseLocked(const NrSessionActorEntry& entry) const noexcept;

        using NrSessionActorMap = std::unordered_map<NrSessionKey, std::unique_ptr<NrSessionActorEntry>>;
        using NrSessionActorKeySet = std::unordered_set<NrSessionKey>;

        mutable psnr::core::NrMutex lock_;
        std::size_t maxSessionCount_ = 0;
        NrSessionActorMap actors_;
        NrSessionActorKeySet closedActorKeys_;
    };
} // namespace psnr::runtime
