#include "pch.h"

#include "NrServer.h"

#include "NrGatewayAccess.h"

#include "NrDiagnosticsConfigInternal.h"
#include "NrDiagnosticsComponent.h"
#include "NrLifecycleInternal.h"
#include "NrMemoryPoolManager.h"

#include "NrServerComponentGraph.h"
#include "NrServerGraphBuilder.h"
#include "NrServerMetrics.h"
#include "NrServerSnapshot.h"
#include "NrServerSnapshotAccess.h"
#include "NrSessionActorRegistry.h"
#include "NrToWorldEventAccess.h"
#include "NrToWorldHandoff.h"

#include <array>
#include <chrono>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    namespace
    {
        [[nodiscard]] NrStatus MapSessionCloseReason(const NrSessionCloseRequestReason reason,
                                                     NrSessionEndReason& outEndReason) noexcept
        {
            switch (reason)
            {
            case NrSessionCloseRequestReason::ApplicationRequested:
                outEndReason = NrSessionEndReason::ApplicationRequested;
                return NrStatus::Success();
            case NrSessionCloseRequestReason::ApplicationPolicy:
                outEndReason = NrSessionEndReason::ApplicationPolicy;
                return NrStatus::Success();
            case NrSessionCloseRequestReason::ProtocolError:
                outEndReason = NrSessionEndReason::ProtocolError;
                return NrStatus::Success();
            case NrSessionCloseRequestReason::None:
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        [[nodiscard]] constexpr bool TryMapMemoryPoolRole(const psnr::core::NrMemoryPoolRole internalRole,
                                                          NrServerMemoryPoolRole& outPublicRole) noexcept
        {
            switch (internalRole)
            {
            case psnr::core::NrMemoryPoolRole::RecvBuffer:
                outPublicRole = NrServerMemoryPoolRole::RecvBuffer;
                return true;
            case psnr::core::NrMemoryPoolRole::OverlappedContext:
                outPublicRole = NrServerMemoryPoolRole::OverlappedContext;
                return true;
            case psnr::core::NrMemoryPoolRole::RuntimeIngressQueueStorage:
                outPublicRole = NrServerMemoryPoolRole::RuntimeIngressQueueStorage;
                return true;
            case psnr::core::NrMemoryPoolRole::ToWorldEventQueueStorage:
                outPublicRole = NrServerMemoryPoolRole::ToWorldEventQueueStorage;
                return true;
            case psnr::core::NrMemoryPoolRole::SessionAcceptRecvMailboxStorage:
                outPublicRole = NrServerMemoryPoolRole::SessionAcceptRecvMailboxStorage;
                return true;
            case psnr::core::NrMemoryPoolRole::SessionSendMailboxStorage:
                outPublicRole = NrServerMemoryPoolRole::SessionSendMailboxStorage;
                return true;
            case psnr::core::NrMemoryPoolRole::ActorReadyQueueStorage:
                outPublicRole = NrServerMemoryPoolRole::ActorReadyQueueStorage;
                return true;
            case psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage:
                return false;
            case psnr::core::NrMemoryPoolRole::Payload64:
                outPublicRole = NrServerMemoryPoolRole::Payload64;
                return true;
            case psnr::core::NrMemoryPoolRole::Payload256:
                outPublicRole = NrServerMemoryPoolRole::Payload256;
                return true;
            case psnr::core::NrMemoryPoolRole::Payload1024:
                outPublicRole = NrServerMemoryPoolRole::Payload1024;
                return true;
            case psnr::core::NrMemoryPoolRole::Payload8192:
                outPublicRole = NrServerMemoryPoolRole::Payload8192;
                return true;
            case psnr::core::NrMemoryPoolRole::PayloadRefControl:
                outPublicRole = NrServerMemoryPoolRole::PayloadRefControl;
                return true;
            case psnr::core::NrMemoryPoolRole::SendBuffer:
            case psnr::core::NrMemoryPoolRole::ClientPayloadQueueStorage:
            case psnr::core::NrMemoryPoolRole::ClientEventQueueStorage:
            case psnr::core::NrMemoryPoolRole::Count:
                return false;
            }

            return false;
        }
    } // namespace

    struct NrServer::Impl
    {
        enum class ServerShellState
        {
            Created,
            Running,
            StopRequested,
            Shutdown,
        };

        Impl(internal::NrDiagnosticsConfigInternal diagnosticsConfigValue, NrServerComponentGraph serverGraph) noexcept
            : diagnosticsConfig(std::move(diagnosticsConfigValue))
            , componentGraph(std::move(serverGraph))
            , bootstrapPlan(componentGraph.BootstrapPlanInput())
        {
        }

        ~Impl() noexcept
        {
            static_cast<void>(Shutdown());
        }

        // Initializing to Created
        [[nodiscard]] NrStatus Configure() noexcept
        {
            if (state != ServerShellState::Created)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            NrBootstrapContext bootstrapContext;
            const NrStatus configureStatus = bootstrapPlan.Configure(bootstrapContext);
            if (configureStatus.Failed())
            {
                state = ServerShellState::Shutdown;
                return configureStatus;
            }

            return NrStatus::Success();
        }

        // Created to Running
        [[nodiscard]] NrStatus Start() noexcept
        {
            if (state != ServerShellState::Created)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            const NrStatus startStatus = bootstrapPlan.Start();
            if (startStatus.Failed())
            {
                state = ServerShellState::Shutdown;
                return startStatus;
            }

            state = ServerShellState::Running;
            return NrStatus::Success();
        }

        // Running to StopRequested
        [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept
        {
            if (state == ServerShellState::StopRequested)
            {
                return NrStatus::Success();
            }

            if (state != ServerShellState::Running)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            const NrStatus submissionStatus = componentGraph.InvalidateSubmissionsAndWait();
            const NrStatus status = bootstrapPlan.RequestStop(context);
            state = ServerShellState::StopRequested;
            return submissionStatus.Failed() ? submissionStatus : status;
        }

        // StopRequested to Shutdown
        [[nodiscard]] NrStatus Shutdown() noexcept
        {
            const NrStatus submissionStatus = componentGraph.InvalidateSubmissionsAndWait();
            if (state == ServerShellState::Shutdown)
            {
                return submissionStatus;
            }

            const NrStatus status = bootstrapPlan.Shutdown();
            state = ServerShellState::Shutdown;
            internal::NrToWorldHandoff* handoff = componentGraph.ToWorldHandoff();
            const NrStatus closeStatus =
                handoff == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : handoff->Close();
            if (submissionStatus.Failed())
            {
                return submissionStatus;
            }
            return status.Failed() ? status : closeStatus;
        }

        [[nodiscard]] NrStatus TryPopToWorldEvent(NrToWorldEvent* const outEvent) noexcept
        {
            if (outEvent == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }
            if (outEvent->IsValid())
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            internal::NrToWorldHandoff* handoff = componentGraph.ToWorldHandoff();
            if (handoff == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            std::unique_ptr<internal::NrToWorldHandoffEvent> event;
            const NrStatus popStatus = handoff->TryPop(event);
            return popStatus.Failed() ? popStatus : internal::NrToWorldEventAccess::Adopt(std::move(event), *outEvent);
        }

        [[nodiscard]] NrStatus TryPopToWorldEvents(NrToWorldEvent* const eventBuffer,
                                                   const std::size_t eventBufferCount,
                                                   std::size_t* const outEventCount) noexcept
        {
            for (std::size_t index = 0; index < eventBufferCount; ++index)
            {
                if (eventBuffer[index].IsValid())
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }
            }

            internal::NrToWorldHandoff* handoff = componentGraph.ToWorldHandoff();
            if (handoff == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            std::array<std::unique_ptr<internal::NrToWorldHandoffEvent>, NrMaxToWorldEventBatchSize>
                internalEventBuffer;
            const std::span<std::unique_ptr<internal::NrToWorldHandoffEvent>> requestedEventBuffer{
                internalEventBuffer.data(),
                eventBufferCount,
            };
            std::size_t eventCount = 0;
            const NrStatus popStatus = handoff->TryPopBatch(requestedEventBuffer, &eventCount);
            if (popStatus.Failed())
            {
                return popStatus;
            }

            for (std::size_t index = 0; index < eventCount; ++index)
            {
                const NrStatus adoptStatus =
                    internal::NrToWorldEventAccess::Adopt(std::move(internalEventBuffer[index]), eventBuffer[index]);
                if (adoptStatus.Failed())
                {
                    return adoptStatus;
                }
            }

            *outEventCount = eventCount;
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus WaitForToWorldEvents(const std::uint32_t timeoutMilliseconds,
                                                    NrToWorldWaitResult* const outWaitResult) noexcept
        {
            if (outWaitResult == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            internal::NrToWorldHandoff* handoff = componentGraph.ToWorldHandoff();
            if (handoff == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            internal::NrToWorldHandoffWaitResult internalResult = internal::NrToWorldHandoffWaitResult::TimedOut;
            const NrStatus waitStatus =
                handoff->WaitForEvents(std::chrono::milliseconds{timeoutMilliseconds}, &internalResult);
            if (waitStatus.Failed())
            {
                return waitStatus;
            }

            switch (internalResult)
            {
            case internal::NrToWorldHandoffWaitResult::EventsAvailable:
                *outWaitResult = NrToWorldWaitResult::EventsAvailable;
                return NrStatus::Success();
            case internal::NrToWorldHandoffWaitResult::TimedOut:
                *outWaitResult = NrToWorldWaitResult::TimedOut;
                return NrStatus::Success();
            case internal::NrToWorldHandoffWaitResult::Closed:
                *outWaitResult = NrToWorldWaitResult::Closed;
                return NrStatus::Success();
            }

            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        [[nodiscard]] NrStatus RequestSessionClose(const NrSessionKey sessionKey,
                                                   const NrSessionCloseRequestReason reason) noexcept
        {
            if (state != ServerShellState::Running)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            NrSessionEndReason endReason = NrSessionEndReason::None;
            const NrStatus reasonStatus = MapSessionCloseReason(reason, endReason);
            return reasonStatus.Failed() ? reasonStatus : componentGraph.RequestSessionClose(sessionKey, endReason);
        }

        [[nodiscard]] NrStatus CreateGateway(NrGateway* const outGateway) noexcept
        {
            if (outGateway == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }
            if (state != ServerShellState::Running)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            psnr::core::NrMemoryPoolManager* memoryPoolManager = componentGraph.MemoryPoolManager();
            if (memoryPoolManager == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            return internal::NrGatewayAccess::Create(componentGraph.SubmissionAdmissionHandle(), *memoryPoolManager,
                                                     *outGateway);
        }

        [[nodiscard]] NrStatus CaptureSnapshot(NrServerSnapshot* const outSnapshot) const noexcept
        {
            if (outSnapshot == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            // metrics for pressure transaction counter
            // pool manager for state of each pool and exhaustion counter
            const internal::NrServerMetrics* metrics = componentGraph.Metrics();
            const psnr::core::NrMemoryPoolManager* memoryPoolManager = componentGraph.MemoryPoolManager();
            const NrSessionActorRegistry* sessionActorRegistry = componentGraph.SessionActorRegistry();
            const internal::NrToWorldHandoff* toWorldHandoff = componentGraph.ToWorldHandoff();
            if (metrics == nullptr || memoryPoolManager == nullptr || sessionActorRegistry == nullptr ||
                toWorldHandoff == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            internal::NrServerSnapshotData data;
            switch (state) // server shell state
            {
            case ServerShellState::Created:
                data.lifecycleState = NrServerLifecycleState::Created;
                break;
            case ServerShellState::Running:
                data.lifecycleState = NrServerLifecycleState::Running;
                break;
            case ServerShellState::StopRequested:
                data.lifecycleState = NrServerLifecycleState::StopRequested;
                break;
            case ServerShellState::Shutdown:
                data.lifecycleState = NrServerLifecycleState::Shutdown;
                break;
            }

            const internal::NrServerMetricsSnapshot metricsSnapshot = metrics->Capture();
            for (std::size_t index = 0; index < NrPressureTransactionOutcomeCount; ++index)
            {
                data.pressureTransactionCounts[index] = metricsSnapshot.pressureTransactionCounts[index];
            }
            data.pendingRecvIoCount = metricsSnapshot.pendingRecvIoCount;
            data.pendingSendIoCount = metricsSnapshot.pendingSendIoCount;
            data.sendMailboxDepth = metricsSnapshot.sendMailboxDepth;
            data.sendMailboxHighWatermark = metricsSnapshot.sendMailboxHighWatermark;
            data.pendingSendQueueDepth = metricsSnapshot.pendingSendQueueDepth;
            data.pendingSendQueueHighWatermark = metricsSnapshot.pendingSendQueueHighWatermark;

            const NrSessionActorRegistryStats registryStats = sessionActorRegistry->Stats();
            data.registeredSessionCount = static_cast<std::uint64_t>(registryStats.registeredSessionCount);
            data.closingSessionCount = static_cast<std::uint64_t>(registryStats.closingSessionCount);

            const internal::NrToWorldHandoffStats toWorldStats = toWorldHandoff->Stats();
            data.toWorldEventDepth = static_cast<std::uint64_t>(toWorldStats.eventDepth);
            data.toWorldEventHighWatermark = static_cast<std::uint64_t>(toWorldStats.eventHighWatermark);

            const psnr::core::NrMemoryPoolManagerStats managerStats = memoryPoolManager->Stats();
            constexpr std::size_t ExhaustedOutcomeIndex = static_cast<std::size_t>(NrPoolPressureOutcome::Exhausted);
            for (const psnr::core::NrMemoryPoolManagerStatsEntry& entry : managerStats.pools)
            {
                NrServerMemoryPoolRole publicRole = NrServerMemoryPoolRole::Count;
                if (!TryMapMemoryPoolRole(entry.role, publicRole))
                {
                    continue;
                }

                const std::size_t roleIndex = static_cast<std::size_t>(publicRole);
                data.memoryPools[roleIndex] = NrServerMemoryPoolSnapshot{
                    static_cast<std::uint64_t>(entry.stats.capacity),
                    static_cast<std::uint64_t>(entry.stats.inUse),
                    static_cast<std::uint64_t>(entry.stats.available),
                    static_cast<std::uint64_t>(entry.stats.highWatermark),
                };
                data.poolAcquirePressureCounts[roleIndex][ExhaustedOutcomeIndex] = entry.stats.failedAcquireCount;
            }

            const internal::NrDiagnosticsStats diagnosticsStats = componentGraph.CaptureDiagnosticsStats();
            data.diagnostics = NrServerDiagnosticsSnapshot{
                diagnosticsStats.enabled,          diagnosticsStats.sinkFailed,
                diagnosticsStats.attempted,        diagnosticsStats.enqueued,
                diagnosticsStats.droppedQueueFull, diagnosticsStats.droppedSinkUnavailable,
                diagnosticsStats.consumed,         diagnosticsStats.discardedAfterSinkFailure,
            };

            internal::NrServerSnapshotAccess::Assign(data, *outSnapshot);
            return NrStatus::Success();
        }

        internal::NrDiagnosticsConfigInternal diagnosticsConfig;
        NrServerComponentGraph componentGraph;
        NrBootstrapPlan bootstrapPlan;

        ServerShellState state = ServerShellState::Created;
    };

    NrServer::NrServer() noexcept {}

    NrServer::NrServer(Impl* impl) noexcept
        : impl_(impl)
    {
    }

    NrServer::NrServer(NrServer&& other) noexcept
        : impl_(std::exchange(other.impl_, nullptr))
    {
    }

    NrServer& NrServer::operator=(NrServer&& other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }

        return *this;
    }

    NrServer::~NrServer() noexcept
    {
        delete impl_;
        impl_ = nullptr;
    }

    NrStatus NrServer::Create(const NrServerConfig& config, NrServer* const outServer) noexcept
    {
        if (outServer == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
        if (outServer->IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        psnr::core::NrResult<internal::NrDiagnosticsConfigInternal> diagnosticsConfigResult =
            internal::CreateDiagnosticsConfigInternal(config.diagnostics);
        if (diagnosticsConfigResult.Failed())
        {
            return diagnosticsConfigResult.Status();
        }
        internal::NrDiagnosticsConfigInternal diagnosticsConfig = diagnosticsConfigResult.TakeValue();

        NrServerComponentGraph componentGraph;
        const NrStatus graphStatus = BuildServerGraph(config, diagnosticsConfig, componentGraph);
        if (graphStatus.Failed())
        {
            return graphStatus;
        }

        Impl* impl = new (std::nothrow) Impl(std::move(diagnosticsConfig), std::move(componentGraph));
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        const NrStatus configureStatus = impl->Configure();
        if (configureStatus.Failed())
        {
            delete impl;
            return configureStatus;
        }

        *outServer = NrServer(impl);
        return NrStatus::Success();
    }

    bool NrServer::IsValid() const noexcept
    {
        return impl_ != nullptr;
    }

    NrStatus NrServer::CreateGateway(NrGateway* const outGateway) noexcept
    {
        if (outGateway == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->CreateGateway(outGateway);
    }

    NrStatus NrServer::Start() noexcept
    {
        if (impl_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return impl_->Start();
    }

    NrStatus NrServer::RequestStop() noexcept
    {
        if (impl_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return impl_->RequestStop(NrStopContext{NrStopReason::Requested, NrStopMode::Graceful});
    }

    NrStatus NrServer::Shutdown() noexcept
    {
        if (impl_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return impl_->Shutdown();
    }

    NrStatus NrServer::TryPopToWorldEvent(NrToWorldEvent* const outEvent) noexcept
    {
        if (outEvent == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->TryPopToWorldEvent(outEvent);
    }

    NrStatus NrServer::TryPopToWorldEvents(NrToWorldEvent* const eventBuffer, const std::size_t eventBufferCount,
                                           std::size_t* const outEventCount) noexcept
    {
        if (eventBuffer == nullptr || eventBufferCount == 0 || eventBufferCount > NrMaxToWorldEventBatchSize ||
            outEventCount == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->TryPopToWorldEvents(eventBuffer, eventBufferCount, outEventCount);
    }

    NrStatus NrServer::WaitForToWorldEvents(const std::uint32_t timeoutMilliseconds,
                                            NrToWorldWaitResult* const outWaitResult) noexcept
    {
        if (outWaitResult == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->WaitForToWorldEvents(timeoutMilliseconds, outWaitResult);
    }

    NrStatus NrServer::RequestSessionClose(const NrSessionKey sessionKey,
                                           const NrSessionCloseRequestReason reason) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->RequestSessionClose(sessionKey, reason);
    }

    NrStatus NrServer::CaptureSnapshot(NrServerSnapshot* const outSnapshot) const noexcept
    {
        if (outSnapshot == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->CaptureSnapshot(outSnapshot);
    }
} // namespace psnr::runtime
