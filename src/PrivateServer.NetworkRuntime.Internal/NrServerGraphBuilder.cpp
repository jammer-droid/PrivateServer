#include "pch.h"

#include "NrServerGraphBuilder.h"

#include "NrErrorCode.h"
#include "NrIocpCompletionPump.h"
#include "NrIocpIoCompletionDispatcher.h"
#include "NrIocpOverlappedContextFactory.h"
#include "NrIocpRuntime.h"
#include "NrIoEventDispatcher.h"
#include "NrListener.h"
#include "NrBoundedMpscQueue.h"
#include "NrDiagnosticsComponent.h"
#include "NrDebugDiagnosticSink.h"
#include "NrDiagnosticRecord.h"
#include "NrDiagnosticsConfigInternal.h"
#include "NrJsonlDiagnosticSink.h"
#include "NrDispatchLane.h"
#include "NrIngressRegistry.h"
#include "NrInput.h"
#include "NrInputFactory.h"
#include "NrIngress.h"
#include "NrMemoryPoolManager.h"
#include "NrMemoryMath.h"
#include "NrPacketDispatchRule.h"
#include "NrPacketDispatchTable.h"
#include "NrPacketParser.h"
#include "NrPacketType.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrServerComponentGraph.h"
#include "NrServerConfig.h"
#include "NrServerSubmissionGate.h"
#include "NrSessionActorRegistry.h"
#include "NrSessionActorScheduler.h"
#include "NrSessionIoEvent.h"
#include "NrSessionIoActor.h"
#include "NrToWorldHandoff.h"
#include "NrSessionIoOperations.h"
#include "NrWinsockRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrMemoryPoolRole;
    using psnr::core::NrResult;
    using psnr::core::NrStatus;
    using NrInputQueue = psnr::core::NrBoundedMpscQueue<psnr::core::NrInput>;

    namespace
    {
        constexpr std::size_t RuntimeIngressCapacity = 16;
        constexpr std::size_t RuntimeRecvBufferCapacity = 8192;
        constexpr psnr::core::NrPacketType HeartbeatPacketType{0};
        constexpr psnr::core::NrPacketType MoveInputPacketType{1};

        using NrToWorldEventQueue = psnr::core::NrBoundedMpscQueue<std::unique_ptr<internal::NrToWorldHandoffEvent>>;
        using NrAcceptRecvMailboxQueue = psnr::core::NrBoundedMpscQueue<psnr::core::NrSessionRecvEvent>;
        using NrSendMailboxQueue = psnr::core::NrBoundedMpscQueue<psnr::core::NrSessionSendEvent>;
        using NrActorReadyQueue = psnr::core::NrBoundedMpscQueue<psnr::core::NrSessionKey>;
        using NrDiagnosticsQueue = psnr::core::NrBoundedMpscQueue<internal::NrDiagnosticRecord>;

        class NrRuntimeQueueIngress final : public psnr::core::NrIngress
        {
        public:
            explicit NrRuntimeQueueIngress(NrInputQueue& queue) noexcept
                : queue_(queue)
            {
            }

            [[nodiscard]] psnr::core::NrStatus TryEnqueue(psnr::core::NrInput&& input) noexcept override
            {
                return queue_.TryPush(std::move(input));
            }

        private:
            NrInputQueue& queue_;
        };

        [[nodiscard]] psnr::core::NrIngressRegistry CreateRuntimeIngressRegistry(
            psnr::core::NrIngress& ingress) noexcept
        {
            const std::array<psnr::core::NrIngressBinding, 1> bindings = {
                psnr::core::NrIngressBinding{psnr::core::NrDispatchLane::ServerIngress, &ingress},
            };

            NrResult<psnr::core::NrIngressRegistry> registryResult =
                psnr::core::NrIngressRegistry::Create(std::span(bindings));

            assert(registryResult.Succeeded());

            return registryResult.TakeValue();
        }

        class NrRuntimeIoPipelineComponent final : public INrServerLifecycleComponent
        {
        public:
            NrRuntimeIoPipelineComponent(NrMemoryPoolManager& memoryPoolManager, NrIocpPort& iocpPort,
                                         NrListener& listener, NrSessionActorRegistry& sessionActorRegistry,
                                         NrActorScheduleHandle actorScheduleHandle, std::size_t recvBufferCapacity,
                                         std::size_t actorMailboxCapacity,
                                         std::size_t pendingSendQueueCapacity,
                                         psnr::core::NrPacketParser packetParser,
                                         psnr::core::NrPacketDispatchTable packetDispatchTable,
                                         std::unique_ptr<NrInputQueue> ingressQueue,
                                         internal::NrToWorldHandoff& toWorldHandoff,
                                         internal::NrServerMetrics& metrics,
                                         internal::NrDiagnosticEmitter diagnosticsEmitter,
                                         internal::NrSubmissionAdmissionHandle submissionAdmission) noexcept
                : iocpPort_(&iocpPort)
                , contextFactory_(memoryPoolManager)
                , sessionIoOperations_(contextFactory_, metrics, diagnosticsEmitter)
                , packetParser_(std::move(packetParser))
                , packetDispatchTable_(std::move(packetDispatchTable))
                , inputFactory_(memoryPoolManager)
                , ingressQueue_(std::move(ingressQueue))
                , ingress_(*ingressQueue_)
                , ingressRegistry_(CreateRuntimeIngressRegistry(ingress_))
                , recvDrainDependencies_{
                      &packetParser_,
                      &packetDispatchTable_,
                      &inputFactory_,
                      &ingressRegistry_,
                      &toWorldHandoff,
                      &metrics,
                  }
                , ioEventDispatcher_(NrIoEventDispatcherDependencies{
                      &listener, &sessionActorRegistry, &sessionIoOperations_, &toWorldHandoff, actorScheduleHandle,
                      std::move(submissionAdmission), diagnosticsEmitter,
                      &memoryPoolManager, recvBufferCapacity, actorMailboxCapacity, pendingSendQueueCapacity,
                      &recvDrainDependencies_})
                , iocpIoCompletionDispatcher_(ioEventDispatcher_)
                , completionPump_(iocpPort, iocpIoCompletionDispatcher_)
            {
                assert(ingressQueue_ != nullptr);
            }

            ~NrRuntimeIoPipelineComponent() noexcept override
            {
                static_cast<void>(Shutdown());
            }

            [[nodiscard]] NrStatus Configure(NrBootstrapContext& context) noexcept override
            {
                static_cast<void>(context);
                return NrStatus::Success();
            }

            [[nodiscard]] NrStatus Start() noexcept override
            {
                if (started_.exchange(true))
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }

                stopRequested_.store(false);

                try
                {
                    workerThread_ = std::thread(&NrRuntimeIoPipelineComponent::WorkerLoop, this);
                }
                catch (const std::system_error&)
                {
                    started_.store(false);
                    return NrStatus::Failure(NrErrorCode::IoFailed);
                }

                return NrStatus::Success();
            }

            [[nodiscard]] NrStatus RequestStop(const NrStopContext& context) noexcept override
            {
                static_cast<void>(context);
                stopRequested_.store(true);
                return WakeWorker();
            }

            [[nodiscard]] NrStatus Shutdown() noexcept override
            {
                stopRequested_.store(true);
                static_cast<void>(WakeWorker());

                if (workerThread_.joinable())
                {
                    workerThread_.join();
                }

                started_.store(false);
                return NrStatus::Success();
            }

        private:
            [[nodiscard]] NrStatus WakeWorker() noexcept
            {
                if (iocpPort_ == nullptr)
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }

                return iocpPort_->PostControlCompletion();
            }

            void WorkerLoop() noexcept
            {
                while (!stopRequested_.load())
                {
                    const NrStatus pumpStatus = completionPump_.PumpOnce();
                    if (pumpStatus.Failed() && !stopRequested_.load())
                    {
                        stopRequested_.store(true);
                    }
                }
            }

            NrIocpPort* iocpPort_ = nullptr;
            NrIocpOverlappedContextFactory contextFactory_;

            NrSessionIoOperations sessionIoOperations_;

            psnr::core::NrPacketParser packetParser_;
            psnr::core::NrPacketDispatchTable packetDispatchTable_;
            psnr::core::NrInputFactory inputFactory_;

            std::unique_ptr<NrInputQueue> ingressQueue_;

            NrRuntimeQueueIngress ingress_;
            psnr::core::NrIngressRegistry ingressRegistry_;
            psnr::core::NrSessionRecvDrainDependencies recvDrainDependencies_;

            NrIoEventDispatcher ioEventDispatcher_;
            NrIocpIoCompletionDispatcher iocpIoCompletionDispatcher_;
            NrIocpCompletionPump completionPump_;

            std::thread workerThread_; // IO pump thread
            std::atomic_bool started_{false};
            std::atomic_bool stopRequested_{false};
        };

        [[nodiscard]] NrResult<psnr::core::NrPacketParser> CreateRuntimePacketParser() noexcept
        {
            return psnr::core::NrPacketParser::Create(psnr::core::NrPacketParserConfig{});
        }

        [[nodiscard]] NrResult<psnr::core::NrPacketDispatchTable> CreateRuntimePacketDispatchTable(
            const NrServerConfig& config) noexcept
        {
            constexpr std::size_t DefaultRuleCount = 2;
            const std::size_t customRuleCount = config.additionalWorldIngressPacketTypes.size;
            try
            {
                std::vector<psnr::core::NrPacketDispatchRule> rules;
                if (customRuleCount > rules.max_size() - DefaultRuleCount)
                {
                    return NrResult<psnr::core::NrPacketDispatchTable>::Failure(NrErrorCode::CapacityExceeded);
                }

                rules.reserve(DefaultRuleCount + customRuleCount);
                rules.push_back(psnr::core::NrPacketDispatchRule{
                    HeartbeatPacketType,
                    psnr::core::NrDispatchLane::ServerIngress,
                });
                rules.push_back(psnr::core::NrPacketDispatchRule{
                    MoveInputPacketType,
                    psnr::core::NrDispatchLane::WorldIngress,
                });

                for (std::uint32_t index = 0; index < config.additionalWorldIngressPacketTypes.size; ++index)
                {
                    rules.push_back(psnr::core::NrPacketDispatchRule{
                        config.additionalWorldIngressPacketTypes.data[index],
                        psnr::core::NrDispatchLane::WorldIngress,
                    });
                }

                return psnr::core::NrPacketDispatchTable::Create(
                    std::span<const psnr::core::NrPacketDispatchRule>(rules.data(), rules.size()));
            }
            catch (const std::bad_alloc&)
            {
                return NrResult<psnr::core::NrPacketDispatchTable>::Failure(NrErrorCode::OutOfMemory);
            }
        }

        [[nodiscard]] constexpr psnr::core::NrMemoryPoolConfig MakePoolConfig(const std::size_t blockSize,
                                                                              const std::size_t blockCount) noexcept
        {
            return psnr::core::NrMemoryPoolConfig{blockSize, blockCount, psnr::core::NrCacheLineSize};
        }

        [[nodiscard]] NrResult<psnr::core::NrMemoryPoolManagerConfig> BuildServerMemoryPoolConfig(
            const NrServerConfig& serverConfig, const NrSessionActorSchedulerConfig& schedulerConfig) noexcept
        {
            std::size_t overlappedContextBlockCount = 0;
            if (!psnr::core::utils::NrTryMultiply(serverConfig.maxSessionCount, 2, overlappedContextBlockCount))
            {
                return NrResult<psnr::core::NrMemoryPoolManagerConfig>::Failure(NrErrorCode::CapacityExceeded);
            }

            const NrResult<std::size_t> runtimeIngressStorageBytesResult =
                NrInputQueue::RequiredStorageBytes(RuntimeIngressCapacity);
            const NrResult<std::size_t> toWorldStorageBytesResult =
                NrToWorldEventQueue::RequiredStorageBytes(serverConfig.toWorldEventCapacity);
            const NrResult<std::size_t> acceptRecvMailboxStorageBytesResult =
                NrAcceptRecvMailboxQueue::RequiredStorageBytes(serverConfig.actorMailboxCapacity);
            const NrResult<std::size_t> sendMailboxStorageBytesResult =
                NrSendMailboxQueue::RequiredStorageBytes(serverConfig.actorMailboxCapacity);
            const NrResult<std::size_t> actorReadyStorageBytesResult =
                NrActorReadyQueue::RequiredStorageBytes(schedulerConfig.maxAdmittedRunnableActors);

            const NrStatus storageStatuses[] = {
                runtimeIngressStorageBytesResult.Status(),    toWorldStorageBytesResult.Status(),
                acceptRecvMailboxStorageBytesResult.Status(), sendMailboxStorageBytesResult.Status(),
                actorReadyStorageBytesResult.Status(),
            };
            for (const NrStatus& status : storageStatuses)
            {
                if (status.Failed())
                {
                    return NrResult<psnr::core::NrMemoryPoolManagerConfig>::Failure(status);
                }
            }

            constexpr std::size_t OverlappedContextBlockSize =
                std::max(sizeof(NrRecvIoContext), sizeof(NrSendIoContext));

            psnr::core::NrMemoryPoolManagerPoolConfig diagnosticsQueuePoolConfig;
            if (serverConfig.diagnostics.mode != NrDiagnosticsMode::Disabled)
            {
                const NrResult<std::size_t> diagnosticsStorageBytesResult =
                    NrDiagnosticsQueue::RequiredStorageBytes(internal::NrDiagnosticsConfigInternal::QueueCapacity);
                if (diagnosticsStorageBytesResult.Failed())
                {
                    return NrResult<psnr::core::NrMemoryPoolManagerConfig>::Failure(
                        diagnosticsStorageBytesResult.Status());
                }

                diagnosticsQueuePoolConfig = psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::DiagnosticsQueueStorage,
                    MakePoolConfig(diagnosticsStorageBytesResult.Value(), 1),
                };
            }

            psnr::core::NrMemoryPoolManagerConfig config;
            config.pools = {
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::RecvBuffer,
                    MakePoolConfig(RuntimeRecvBufferCapacity, serverConfig.maxSessionCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::OverlappedContext,
                    MakePoolConfig(OverlappedContextBlockSize, overlappedContextBlockCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::RuntimeIngressQueueStorage,
                                                          MakePoolConfig(runtimeIngressStorageBytesResult.Value(), 1)},
                psnr::core::NrMemoryPoolManagerPoolConfig{NrMemoryPoolRole::ToWorldEventQueueStorage,
                                                          MakePoolConfig(toWorldStorageBytesResult.Value(), 1)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::SessionAcceptRecvMailboxStorage,
                    MakePoolConfig(acceptRecvMailboxStorageBytesResult.Value(), serverConfig.maxSessionCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::SessionSendMailboxStorage,
                    MakePoolConfig(sendMailboxStorageBytesResult.Value(), serverConfig.maxSessionCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::ActorReadyQueueStorage,
                    MakePoolConfig(actorReadyStorageBytesResult.Value(), schedulerConfig.actorWorkerCount)},
                diagnosticsQueuePoolConfig,
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::Payload64, MakePoolConfig(64, serverConfig.payloadPools.payload64BlockCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::Payload256, MakePoolConfig(256, serverConfig.payloadPools.payload256BlockCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::Payload1024,
                    MakePoolConfig(1024, serverConfig.payloadPools.payload1024BlockCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::Payload8192,
                    MakePoolConfig(8192, serverConfig.payloadPools.payload8192BlockCount)},
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    NrMemoryPoolRole::PayloadRefControl,
                    MakePoolConfig(64, serverConfig.payloadPools.payloadRefControlBlockCount)},
            };
            return NrResult<psnr::core::NrMemoryPoolManagerConfig>(config);
        }

        [[nodiscard]] NrResult<std::unique_ptr<NrInputQueue>> CreateRuntimeIngressQueue(
            NrMemoryPoolManager& memoryPoolManager) noexcept
        {
            return NrInputQueue::Create(memoryPoolManager, NrMemoryPoolRole::RuntimeIngressQueueStorage,
                                        RuntimeIngressCapacity);
        }

        template <typename TComponent, typename... Args>
        [[nodiscard]] NrStatus AddOwnedComponentAndGet(NrServerComponentGraph& graph, TComponent*& addedComponent,
                                                       Args&&... args) noexcept
        {
            std::unique_ptr<TComponent> component(new (std::nothrow) TComponent(std::forward<Args>(args)...));
            if (component == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            TComponent* rawComponent = component.get();
            const NrStatus addStatus = graph.AddOwnedComponent(std::move(component));
            if (addStatus.Failed())
            {
                return addStatus;
            }

            addedComponent = rawComponent;
            return NrStatus::Success();
        }

        template <typename TComponent, typename... Args>
        [[nodiscard]] NrStatus AddOwnedComponent(NrServerComponentGraph& graph, Args&&... args) noexcept
        {
            TComponent* unusedComponent = nullptr;
            return AddOwnedComponentAndGet<TComponent>(graph, unusedComponent, std::forward<Args>(args)...);
        }

        [[nodiscard]] NrStatus ValidateServerConfig(const NrServerConfig& config) noexcept
        {
            if (config.bindEndpoint.port == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            if (config.listenBacklog <= 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            if (config.acceptSlotCount == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            if (config.actorMailboxCapacity == 0 || config.pendingSendQueueCapacity == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            if (config.maxSessionCount == 0 || config.toWorldEventCapacity == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            if (config.payloadPools.payload64BlockCount == 0 || config.payloadPools.payload256BlockCount == 0 ||
                config.payloadPools.payload1024BlockCount == 0 || config.payloadPools.payload8192BlockCount == 0 ||
                config.payloadPools.payloadRefControlBlockCount == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            if (config.additionalWorldIngressPacketTypes.size > 0 &&
                config.additionalWorldIngressPacketTypes.data == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            return NrStatus::Success();
        }

        [[nodiscard]] NrListenerConfig BuildListenerConfig(const NrServerConfig& config) noexcept
        {
            return NrListenerConfig{config.bindEndpoint, config.listenBacklog, config.acceptSlotCount};
        }

        [[nodiscard]] std::unique_ptr<NrListener> CreateListener(
            const NrServerConfig& config, NrIocpPort& iocpPort,
            const internal::NrDiagnosticEmitter diagnosticsEmitter) noexcept
        {
            return std::unique_ptr<NrListener>(new (std::nothrow) NrListener(
                BuildListenerConfig(config), NrListenerDependencies{&iocpPort, diagnosticsEmitter}));
        }

        [[nodiscard]] NrStatus AddWinsockRuntime(NrServerComponentGraph& graph) noexcept
        {
            return AddOwnedComponent<NrWinsockRuntime>(graph);
        }

        [[nodiscard]] NrStatus AddDiagnosticsComponent(const internal::NrDiagnosticsConfigInternal& config,
                                                       std::unique_ptr<internal::INrDiagnosticSink> sink,
                                                       NrServerComponentGraph& graph) noexcept
        {
            if (sink == nullptr)
            {
                return NrStatus::Success();
            }

            NrMemoryPoolManager* memoryPoolManager = graph.MemoryPoolManager();
            if (memoryPoolManager == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            NrResult<std::unique_ptr<internal::NrDiagnosticsComponent>> componentResult =
                internal::NrDiagnosticsComponent::Create(config, *memoryPoolManager, std::move(sink));
            if (componentResult.Failed())
            {
                return componentResult.Status();
            }

            std::unique_ptr<internal::NrDiagnosticsComponent> component = componentResult.TakeValue();
            internal::NrDiagnosticsComponent* componentRaw = component.get();
            const NrStatus addStatus = graph.AddOwnedComponent(std::move(component));
            return addStatus.Failed() ? addStatus : graph.AttachDiagnosticsComponent(*componentRaw);
        }

        [[nodiscard]] NrStatus CreateBuiltInDiagnosticsSink(
            const internal::NrDiagnosticsConfigInternal& config,
            std::unique_ptr<internal::INrDiagnosticSink>& outSink) noexcept
        {
            if (outSink != nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            switch (config.mode)
            {
            case NrDiagnosticsMode::Disabled:
                return NrStatus::Success();

            case NrDiagnosticsMode::Debug:
            {
                NrResult<std::unique_ptr<internal::INrDiagnosticSink>> result = internal::CreateDebugDiagnosticSink();
                if (result.Failed())
                {
                    return result.Status();
                }
                outSink = result.TakeValue();
                return NrStatus::Success();
            }

            case NrDiagnosticsMode::Benchmark:
            {
                NrResult<std::unique_ptr<internal::INrDiagnosticSink>> result =
                    internal::CreateJsonlDiagnosticSink(config.outputPath);
                if (result.Failed())
                {
                    return result.Status();
                }
                outSink = result.TakeValue();
                return NrStatus::Success();
            }
            }

            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        [[nodiscard]] NrStatus AddIocpRuntime(NrServerComponentGraph& graph, NrIocpRuntime*& iocpRuntime) noexcept
        {
            return AddOwnedComponentAndGet<NrIocpRuntime>(graph, iocpRuntime);
        }

        [[nodiscard]] NrStatus AddSessionActorRuntime(std::size_t maxSessionCount,
                                                      const NrSessionActorSchedulerConfig& schedulerConfig,
                                                      NrServerComponentGraph& graph,
                                                      NrSessionActorRegistry*& sessionActorRegistry,
                                                      NrSessionActorScheduler*& sessionActorScheduler) noexcept
        {
            NrMemoryPoolManager* memoryPoolManager = graph.MemoryPoolManager();
            internal::NrServerMetrics* metrics = graph.Metrics();
            if (memoryPoolManager == nullptr || metrics == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            const NrStatus registryStatus =
                AddOwnedComponentAndGet<NrSessionActorRegistry>(graph, sessionActorRegistry, maxSessionCount);
            if (registryStatus.Failed())
            {
                return registryStatus;
            }

            const NrStatus attachRegistryStatus = graph.AttachSessionActorRegistry(*sessionActorRegistry);
            if (attachRegistryStatus.Failed())
            {
                return attachRegistryStatus;
            }

            return AddOwnedComponentAndGet<NrSessionActorScheduler>(graph, sessionActorScheduler, *sessionActorRegistry,
                                                                    *memoryPoolManager, *metrics, schedulerConfig,
                                                                    graph.DiagnosticsEmitter());
        }

        [[nodiscard]] NrStatus AddIoPipelineAndListener(
            const NrServerConfig& config, NrIocpPort& iocpPort, NrSessionActorRegistry& sessionActorRegistry,
            NrActorScheduleHandle actorScheduleHandle, const internal::NrSubmissionAdmissionHandle& submissionAdmission,
            NrServerComponentGraph& graph) noexcept
        {
            const internal::NrDiagnosticEmitter diagnosticsEmitter = graph.DiagnosticsEmitter();
            std::unique_ptr<NrListener> listener = CreateListener(config, iocpPort, diagnosticsEmitter);
            if (listener == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            NrMemoryPoolManager* memoryPoolManager = graph.MemoryPoolManager();
            if (memoryPoolManager == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            internal::NrServerMetrics* metrics = graph.Metrics();
            if (metrics == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }
            NrResult<std::unique_ptr<internal::NrToWorldHandoff>> handoffResult = internal::NrToWorldHandoff::Create(
                *memoryPoolManager, *metrics, diagnosticsEmitter, config.maxSessionCount, config.toWorldEventCapacity);
            if (handoffResult.Failed())
            {
                return handoffResult.Status();
            }

            std::unique_ptr<internal::NrToWorldHandoff> handoff = handoffResult.TakeValue();
            internal::NrToWorldHandoff* handoffRaw = handoff.get();
            NrResult<psnr::core::NrPacketParser> packetParserResult = CreateRuntimePacketParser();
            if (packetParserResult.Failed())
            {
                return packetParserResult.Status();
            }

            NrResult<psnr::core::NrPacketDispatchTable> packetDispatchTableResult =
                CreateRuntimePacketDispatchTable(config);
            if (packetDispatchTableResult.Failed())
            {
                return packetDispatchTableResult.Status();
            }

            NrResult<std::unique_ptr<NrInputQueue>> ingressQueueResult = CreateRuntimeIngressQueue(*memoryPoolManager);
            if (ingressQueueResult.Failed())
            {
                return ingressQueueResult.Status();
            }

            NrListener* listenerRaw = listener.get();
            std::unique_ptr<NrRuntimeIoPipelineComponent> pipeline(new (std::nothrow) NrRuntimeIoPipelineComponent(
                *memoryPoolManager, iocpPort, *listenerRaw, sessionActorRegistry, actorScheduleHandle,
                RuntimeRecvBufferCapacity, config.actorMailboxCapacity, config.pendingSendQueueCapacity,
                packetParserResult.TakeValue(), packetDispatchTableResult.TakeValue(), ingressQueueResult.TakeValue(),
                *handoffRaw, *metrics, diagnosticsEmitter, submissionAdmission));
            if (pipeline == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }

            const NrStatus pipelineStatus = graph.AddOwnedComponent(std::move(pipeline));
            if (pipelineStatus.Failed())
            {
                return pipelineStatus;
            }

            const NrStatus handoffStatus = graph.AttachToWorldHandoff(std::move(handoff));
            if (handoffStatus.Failed())
            {
                return handoffStatus;
            }

            return graph.AddOwnedComponent(std::move(listener));
        }
    } // namespace

    NrStatus BuildServerGraph(const NrServerConfig& config,
                              const internal::NrDiagnosticsConfigInternal& diagnosticsConfig,
                              NrServerComponentGraph& graph,
                              std::unique_ptr<internal::INrDiagnosticSink> diagnosticsSink) noexcept
    {
        const NrStatus validationStatus = ValidateServerConfig(config);
        if (validationStatus.Failed())
        {
            return validationStatus;
        }
        if (config.diagnostics.mode != diagnosticsConfig.mode)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        const NrSessionActorSchedulerConfig schedulerConfig;
        NrResult<psnr::core::NrMemoryPoolManagerConfig> memoryPoolConfigResult =
            BuildServerMemoryPoolConfig(config, schedulerConfig);
        if (memoryPoolConfigResult.Failed())
        {
            return memoryPoolConfigResult.Status();
        }
        const psnr::core::NrMemoryPoolManagerConfig memoryPoolConfig = memoryPoolConfigResult.TakeValue();

        const NrStatus metricsStatus = graph.InitializeMetrics();
        if (metricsStatus.Failed())
        {
            return metricsStatus;
        }

        const NrStatus memoryPoolStatus = graph.InitializeMemoryPoolManager(memoryPoolConfig);
        if (memoryPoolStatus.Failed())
        {
            return memoryPoolStatus;
        }

        const NrStatus submissionGateStatus = graph.InitializeSubmissionGate();
        if (submissionGateStatus.Failed())
        {
            return submissionGateStatus;
        }

        if (diagnosticsSink == nullptr)
        {
            const NrStatus sinkStatus = CreateBuiltInDiagnosticsSink(diagnosticsConfig, diagnosticsSink);
            if (sinkStatus.Failed())
            {
                return sinkStatus;
            }
        }

        const NrStatus diagnosticsStatus =
            AddDiagnosticsComponent(diagnosticsConfig, std::move(diagnosticsSink), graph);
        if (diagnosticsStatus.Failed())
        {
            return diagnosticsStatus;
        }

        const internal::NrSubmissionAdmissionHandle submissionAdmission = graph.SubmissionAdmissionHandle();

        const NrStatus winsockStatus = AddWinsockRuntime(graph);
        if (winsockStatus.Failed())
        {
            return winsockStatus;
        }

        NrIocpRuntime* iocpRuntime = nullptr;
        const NrStatus iocpStatus = AddIocpRuntime(graph, iocpRuntime);
        if (iocpStatus.Failed())
        {
            return iocpStatus;
        }

        NrSessionActorRegistry* sessionActorRegistry = nullptr;
        NrSessionActorScheduler* sessionActorScheduler = nullptr;
        const NrStatus sessionActorStatus = AddSessionActorRuntime(config.maxSessionCount, schedulerConfig, graph,
                                                                   sessionActorRegistry, sessionActorScheduler);
        if (sessionActorStatus.Failed())
        {
            return sessionActorStatus;
        }

        const NrStatus sessionCloseRouteStatus =
            graph.AttachSessionActorScheduleHandle(sessionActorScheduler->ScheduleHandle());
        if (sessionCloseRouteStatus.Failed())
        {
            return sessionCloseRouteStatus;
        }

        const NrStatus pipelineStatus =
            AddIoPipelineAndListener(config, iocpRuntime->Port(), *sessionActorRegistry,
                                     sessionActorScheduler->ScheduleHandle(), submissionAdmission, graph);
        if (pipelineStatus.Failed())
        {
            return pipelineStatus;
        }

        return graph.RebuildLifecycleOrder();
    }
} // namespace psnr::runtime
