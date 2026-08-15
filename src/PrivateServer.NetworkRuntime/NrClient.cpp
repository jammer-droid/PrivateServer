#include "pch.h"

#include "NrClient.h"

#include "NrBoundedMpscQueue.h"
#include "NrClientCommandChannel.h"
#include "NrClientEventAccess.h"
#include "NrClientIoWorker.h"
#include "NrClientLifecycleStateMachine.h"
#include "NrClientMemoryPoolConfigFactory.h"
#include "NrClientPendingSend.h"
#include "NrClientSnapshotAccess.h"
#include "NrClientTransport.h"
#include "NrClientTransportEventSink.h"
#include "NrErrorCode.h"
#include "NrIocpPort.h"
#include "NrMemoryMath.h"
#include "NrMemoryPoolManager.h"
#include "NrWinsockRuntime.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    namespace
    {
        // IOCP worker -> NrClientEvent queue -> caller -> TryPopEvent (From Server To Client)
        using NrClientEventQueue = psnr::core::NrBoundedMpscQueue<NrClientEvent>;

        // caller -> NrClientPendingSend queue -> IOCP worker -> WSASend (From Client To Server)
        using NrClientPendingSendQueue = internal::NrClientPendingSendQueue;
    } // namespace

    struct NrClient::Impl final : internal::INrClientTransportEventSink
    {
        Impl(const NrClientConfig& config, std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager,
             NrIocpPort iocpPort, std::unique_ptr<NrClientEventQueue> clientEventQueue,
             std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue) noexcept
            : clientEventQueueCapacity(config.eventQueueCapacity)
            , memoryPoolManager(std::move(memoryPoolManager))
            , clientEventQueue(std::move(clientEventQueue))
            , pendingSendQueue(std::move(pendingSendQueue))
            , iocpPort(std::move(iocpPort))
        {
        }

        ~Impl() noexcept
        {
            static_cast<void>(Shutdown());
        }

        [[nodiscard]] NrStatus InitializeRuntime() noexcept
        {
            const NrStatus winsockStatus = winsockRuntime.Start();
            if (winsockStatus.Failed())
            {
                return winsockStatus;
            }

            commandChannel.reset(new (std::nothrow)
                                     internal::NrClientCommandChannel(iocpPort, *memoryPoolManager, *pendingSendQueue));
            if (commandChannel == nullptr)
            {
                static_cast<void>(winsockRuntime.Shutdown());
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            transport.reset(new (std::nothrow) internal::NrClientTransport(
                iocpPort, *memoryPoolManager, *commandChannel, lifecycleStateMachine, *this));
            if (transport == nullptr)
            {
                commandChannel.reset();
                static_cast<void>(winsockRuntime.Shutdown());
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            worker.reset(new (std::nothrow) internal::NrClientIoWorker(iocpPort, *transport));
            if (worker == nullptr)
            {
                transport.reset();
                commandChannel.reset();
                static_cast<void>(winsockRuntime.Shutdown());
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            const NrStatus startStatus = worker->Start();
            if (startStatus.Failed())
            {
                worker.reset();
                transport.reset();
                commandChannel.reset();
                static_cast<void>(winsockRuntime.Shutdown());
                return startStatus;
            }

            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus TryPopEvent(NrClientEvent& outEvent) noexcept
        {
            if (outEvent.IsValid() || commandChannel == nullptr ||
                commandChannel->State() == NrClientLifecycleState::Shutdown)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            const NrStatus popStatus = clientEventQueue->TryPop(outEvent);
            if (popStatus.Failed())
            {
                return popStatus;
            }

            static_cast<void>(clientEventQueueDepth.fetch_sub(1, std::memory_order_relaxed));
            if (pendingLifecycleEvent.load(std::memory_order_acquire)) // promote pending lifecycle event if exist
            {
                const bool wasWakePending = eventSpaceAvailableWakePending.exchange(true, std::memory_order_acq_rel);
                if (!wasWakePending)
                {
                    // wake POST가 안된 경우
                    const NrStatus wakeStatus = commandChannel->SignalEventSpaceAvailable();
                    if (wakeStatus.Failed())
                    {
                        eventSpaceAvailableWakePending.store(false, std::memory_order_release);
                    }
                }
            }
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus CaptureSnapshot(NrClientSnapshot& outSnapshot) const noexcept
        {
            internal::NrClientSnapshotData data;
            data.lifecycleState = commandChannel == nullptr ? lifecycleStateMachine.State() : commandChannel->State();
            if (transport != nullptr)
            {
                data.pendingConnectIoCount = transport->PendingConnectIoCount();
                data.pendingRecvIoCount = transport->PendingRecvIoCount();
                data.pendingSendIoCount = transport->PendingSendIoCount();
            }
            data.eventQueueDepth = static_cast<std::uint64_t>(clientEventQueueDepth.load(std::memory_order_relaxed));
            data.eventQueueHighWatermark =
                static_cast<std::uint64_t>(clientEventQueueHighWatermark.load(std::memory_order_relaxed));
            if (commandChannel != nullptr)
            {
                data.pendingSendQueueDepth = static_cast<std::uint64_t>(commandChannel->PendingSendQueueDepth());
                data.pendingSendQueueHighWatermark =
                    static_cast<std::uint64_t>(commandChannel->PendingSendQueueHighWatermark());
            }
            internal::NrClientSnapshotAccess::Assign(data, outSnapshot);
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus Shutdown() noexcept
        {
            if (worker == nullptr || commandChannel == nullptr)
            {
                const NrStatus lifecycleStatus = lifecycleStateMachine.Shutdown();
                ClearEventStorage();
                static_cast<void>(winsockRuntime.Shutdown());
                return lifecycleStatus;
            }

            if (worker->IsRunning())
            {
                const NrStatus submitStatus = commandChannel->SubmitShutdown();
                if (submitStatus.Failed())
                {
                    return submitStatus;
                }
            }

            const NrStatus joinStatus = worker->Join(); // wait for IOCP worker thread
            ClearEventStorage();
            const NrStatus winsockStatus = winsockRuntime.Shutdown();
            return joinStatus.Failed() ? joinStatus : winsockStatus;
        }

        [[nodiscard]] NrStatus TryPushEvent(NrClientEvent&& event) noexcept
        {
            if (!event.IsValid())
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            const std::size_t depth = clientEventQueueDepth.fetch_add(1, std::memory_order_relaxed) + 1;
            const NrStatus pushStatus = clientEventQueue->TryPush(std::move(event));
            if (pushStatus.Failed())
            {
                static_cast<void>(clientEventQueueDepth.fetch_sub(1, std::memory_order_relaxed));
                return pushStatus;
            }

            UpdateHighWatermark(clientEventQueueHighWatermark, depth);
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus PublishTransportConnected() noexcept override
        {
            NrClientEvent event;
            const NrStatus createStatus = internal::NrClientEventAccess::CreateTransportConnected(event);
            if (createStatus.Failed())
            {
                return createStatus;
            }

            return PublishLifecycleEvent(std::move(event), pendingAttemptOutcome);
        }

        [[nodiscard]] NrStatus PublishTransportConnectionFailed(const NrStatus transportStatus) noexcept override
        {
            NrClientEvent event;
            const NrStatus createStatus =
                internal::NrClientEventAccess::CreateTransportConnectionFailed(transportStatus, event);
            if (createStatus.Failed())
            {
                return createStatus;
            }

            return PublishLifecycleEvent(std::move(event), pendingAttemptOutcome);
        }

        [[nodiscard]] NrStatus PublishPacketReceived(const NrPacketType packetType,
                                                     const std::span<const std::byte> payload) noexcept override
        {
            if (pendingLifecycleEvent.load(std::memory_order_acquire) ||
                clientEventQueue->SizeApprox() == clientEventQueueCapacity)
            {
                return NrStatus::Failure(NrErrorCode::QueueFull);
            }

            NrClientEvent event;
            const NrStatus createStatus =
                internal::NrClientEventAccess::CreatePacketReceived(packetType, payload, event);
            if (createStatus.Failed())
            {
                return createStatus;
            }

            return TryPushEvent(std::move(event));
        }

        [[nodiscard]] NrStatus PublishTransportDisconnected(const NrClientDisconnectReason reason,
                                                            const NrStatus transportStatus) noexcept override
        {
            NrClientEvent event;
            const NrStatus createStatus =
                internal::NrClientEventAccess::CreateTransportDisconnected(reason, transportStatus, event);
            if (createStatus.Failed())
            {
                return createStatus;
            }

            return PublishLifecycleEvent(std::move(event), pendingDisconnected);
        }

        [[nodiscard]] NrStatus HandleEventSpaceAvailable() noexcept override
        {
            const bool wasWakePending = eventSpaceAvailableWakePending.exchange(false, std::memory_order_acq_rel);
            if (!wasWakePending)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            return TryPublishPendingLifecycleEvents();
        }

        [[nodiscard]] NrStatus TryPublishPendingLifecycleEvents() noexcept
        {
            if (pendingAttemptOutcome.IsValid()) // connection first
            {
                const NrStatus status = TryPushEvent(std::move(pendingAttemptOutcome));
                if (status.Failed())
                {
                    return status;
                }
            }

            if (pendingDisconnected.IsValid()) // disconnected second
            {
                const NrStatus status = TryPushEvent(std::move(pendingDisconnected));
                if (status.Failed())
                {
                    return status;
                }
            }

            pendingLifecycleEvent.store(false, std::memory_order_release);
            return NrStatus::Success();
        }

        void ClearEventStorage() noexcept
        {
            NrClientEvent event;
            while (clientEventQueue->TryPop(event).Succeeded())
            {
                event = NrClientEvent{};
            }

            pendingAttemptOutcome = NrClientEvent{};
            pendingDisconnected = NrClientEvent{};
            clientEventQueueDepth.store(0, std::memory_order_relaxed);
            pendingLifecycleEvent.store(false, std::memory_order_relaxed);
            eventSpaceAvailableWakePending.store(false, std::memory_order_relaxed);
        }

        [[nodiscard]] NrStatus PublishLifecycleEvent(NrClientEvent&& event, NrClientEvent& pendingSlot) noexcept
        {
            if (pendingSlot.IsValid())
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            if (clientEventQueue->SizeApprox() == clientEventQueueCapacity) // queue full
            {
                pendingSlot = std::move(event);
                pendingLifecycleEvent.store(true, std::memory_order_release);
                static_cast<void>(TryPublishPendingLifecycleEvents());
                return NrStatus::Success();
            }

            return TryPushEvent(std::move(event));
        }

        static void UpdateHighWatermark(std::atomic<std::size_t>& highWatermark, std::size_t depth) noexcept
        {
            std::size_t observed = highWatermark.load(std::memory_order_relaxed);
            while (observed < depth && !highWatermark.compare_exchange_weak(observed, depth, std::memory_order_relaxed,
                                                                            std::memory_order_relaxed))
            {
            }
        }

        NrWinsockRuntime winsockRuntime;
        internal::NrClientLifecycleStateMachine lifecycleStateMachine;
        const std::size_t clientEventQueueCapacity;
        std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager;
        std::unique_ptr<NrClientEventQueue> clientEventQueue;       // caller-facing event queue
        std::unique_ptr<NrClientPendingSendQueue> pendingSendQueue; // server-facing pending send queue
        NrIocpPort iocpPort;

        std::atomic<std::size_t> clientEventQueueDepth{0};
        std::atomic<std::size_t> clientEventQueueHighWatermark{0};

        NrClientEvent pendingAttemptOutcome; // pending Connected/ConnectionFailed outcome
        NrClientEvent pendingDisconnected;   // pending Disconnected

        // pending lifecycle event 처리용 atomic 변수
        std::atomic<bool> pendingLifecycleEvent{false}; // 1. Pending 상태인 lifecycle event 존재 여부
        std::atomic<bool> eventSpaceAvailableWakePending{
            false}; // 2. 존재한다면 해당 event 처리를 위한 IOCP wake가 Post 됐는지

        std::unique_ptr<internal::NrClientCommandChannel> commandChannel;
        std::unique_ptr<internal::NrClientTransport> transport;
        std::unique_ptr<internal::NrClientIoWorker> worker;
    };

    NrClient::NrClient() noexcept = default;

    NrClient::NrClient(NrClient&& other) noexcept
        : impl_(std::exchange(other.impl_, nullptr))
    {
    }

    NrClient& NrClient::operator=(NrClient&& other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }

        return *this;
    }

    NrClient::~NrClient() noexcept
    {
        delete impl_;
        impl_ = nullptr;
    }

    NrStatus NrClient::Create(const NrClientConfig& config, NrClient* outClient) noexcept
    {
        if (outClient == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (outClient->IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (config.eventQueueCapacity < 2 || !psnr::core::utils::NrIsPowerOfTwo(config.eventQueueCapacity) ||
            config.payloadQueueCapacity < 2 || !psnr::core::utils::NrIsPowerOfTwo(config.payloadQueueCapacity))
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrIocpPort iocpPort;
        const NrStatus iocpStatus = iocpPort.Create();
        if (iocpStatus.Failed())
        {
            return iocpStatus;
        }

        const psnr::core::NrResult<std::size_t> clientEventQueueStorageResult =
            NrClientEventQueue::RequiredStorageBytes(config.eventQueueCapacity);
        if (clientEventQueueStorageResult.Failed())
        {
            return clientEventQueueStorageResult.Status();
        }

        const psnr::core::NrResult<std::size_t> pendingSendQueueStorageResult =
            NrClientPendingSendQueue::RequiredStorageBytes(config.payloadQueueCapacity);
        if (pendingSendQueueStorageResult.Failed())
        {
            return pendingSendQueueStorageResult.Status();
        }

        internal::NrClientMemoryPoolSizing sizing;
        sizing.eventQueueStorageBytes = clientEventQueueStorageResult.Value();
        sizing.payloadQueueStorageBytes = pendingSendQueueStorageResult.Value();
        sizing.payloadQueueCapacity = config.payloadQueueCapacity;

        psnr::core::NrResult<psnr::core::NrMemoryPoolManagerConfig> memoryPoolConfigResult =
            internal::NrClientMemoryPoolConfigFactory::Create(sizing);
        if (memoryPoolConfigResult.Failed())
        {
            return memoryPoolConfigResult.Status();
        }

        psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> memoryPoolManagerResult =
            psnr::core::NrMemoryPoolManager::Create(memoryPoolConfigResult.Value());
        if (memoryPoolManagerResult.Failed())
        {
            return memoryPoolManagerResult.Status();
        }
        std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager = memoryPoolManagerResult.TakeValue();

        psnr::core::NrResult<std::unique_ptr<NrClientEventQueue>> clientEventQueueResult = NrClientEventQueue::Create(
            *memoryPoolManager, psnr::core::NrMemoryPoolRole::ClientEventQueueStorage, config.eventQueueCapacity);
        if (clientEventQueueResult.Failed())
        {
            return clientEventQueueResult.Status();
        }

        psnr::core::NrResult<std::unique_ptr<NrClientPendingSendQueue>> pendingSendQueueResult =
            NrClientPendingSendQueue::Create(*memoryPoolManager,
                                             psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage,
                                             config.payloadQueueCapacity);
        if (pendingSendQueueResult.Failed())
        {
            return pendingSendQueueResult.Status();
        }

        Impl* impl = new (std::nothrow) Impl(config, std::move(memoryPoolManager), std::move(iocpPort),
                                             clientEventQueueResult.TakeValue(), pendingSendQueueResult.TakeValue());
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        const NrStatus initializeStatus = impl->InitializeRuntime();
        if (initializeStatus.Failed())
        {
            delete impl;
            return initializeStatus;
        }

        *outClient = NrClient(impl);
        return NrStatus::Success();
    }

    bool NrClient::IsValid() const noexcept
    {
        return impl_ != nullptr;
    }

    NrStatus NrClient::Connect(const NrEndpoint& endpoint) noexcept
    {
        if (impl_ == nullptr || impl_->commandChannel == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (impl_->commandChannel->State() != NrClientLifecycleState::Idle)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        std::uint64_t attemptGeneration = 0;
        return impl_->commandChannel->SubmitConnect(endpoint, attemptGeneration);
    }

    NrStatus NrClient::Disconnect() noexcept
    {
        return impl_ == nullptr || impl_->commandChannel == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                                                    : impl_->commandChannel->SubmitDisconnect();
    }

    NrStatus NrClient::Shutdown() noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->Shutdown();
    }

    NrStatus NrClient::Send(const NrPacketType packetType, const NrByteView payload) noexcept
    {
        if (impl_ == nullptr || impl_->commandChannel == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (payload.size != 0 && payload.data == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        const std::span<const std::byte> semanticPayload(payload.data, payload.size);
        return impl_->commandChannel->SubmitSend(packetType, semanticPayload);
    }

    NrStatus NrClient::TryPopEvent(NrClientEvent* outEvent) noexcept
    {
        if (outEvent == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->TryPopEvent(*outEvent);
    }

    NrStatus NrClient::CaptureSnapshot(NrClientSnapshot* outSnapshot) const noexcept
    {
        if (outSnapshot == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->CaptureSnapshot(*outSnapshot);
    }

    NrClient::NrClient(Impl* impl) noexcept
        : impl_(impl)
    {
    }
} // namespace psnr::runtime
