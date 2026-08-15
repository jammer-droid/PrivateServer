#include "pch.h"

#include "NrAcceptIoContext.h"
#include "NrBoundedMpscQueue.h"
#include "NrDiagnosticEmitter.h"
#include "NrDiagnosticsComponent.h"
#include "NrDiagnosticsConfigInternal.h"
#include "NrDiagnosticRecord.h"
#include "NrDiagnosticSink.h"
#include "NrIoEventDispatcher.h"
#include "NrListener.h"
#include "NrMemoryPoolManager.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrServerComponentGraph.h"
#include "NrServerConfig.h"
#include "NrServerGraphBuilder.h"
#include "NrServerMetrics.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace psnr::runtime
{
    class NrIoEventDispatcherTestAccess final
    {
    public:
        static NrStatus RecordSessionBootstrapFailure(NrIoEventDispatcher& dispatcher,
                                                      const psnr::core::NrSessionKey sessionKey,
                                                      const NrStatus& status) noexcept
        {
            return dispatcher.RecordSessionBootstrapFailure(sessionKey, status);
        }
    };

    class NrSessionActorSchedulerTestAccess final
    {
    public:
        static void EmitAdmissionAnomaly(NrSessionActorScheduler& scheduler, const psnr::core::NrSessionKey sessionKey,
                                         const NrStatus& status) noexcept
        {
            scheduler.EmitAdmissionAnomaly(sessionKey, status);
        }
    };
} // namespace psnr::runtime

namespace psnr::runtime::internal
{
    namespace
    {
        using NrDiagnosticsQueue = psnr::core::NrBoundedMpscQueue<NrDiagnosticRecord>;

        struct NrCapturingDiagnosticSinkState final
        {
            NrDiagnosticRunMetadata metadata{};
            NrDiagnosticSummary summary{};
            NrDiagnosticRecord lastRecord{};
            std::vector<NrDiagnosticRecord> records;
            std::thread::id beginThread{};
            std::thread::id consumeThread{};
            std::thread::id finishThread{};
            std::uint32_t beginCount = 0;
            std::uint32_t consumeCount = 0;
            std::uint32_t finishCount = 0;
            bool failBegin = false;
            bool failFinish = false;
        };

        class NrCapturingDiagnosticSink final : public INrDiagnosticSink
        {
        public:
            explicit NrCapturingDiagnosticSink(std::shared_ptr<NrCapturingDiagnosticSinkState> state) noexcept
                : state_(std::move(state))
            {
            }

            [[nodiscard]] psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata& metadata) noexcept override
            {
                state_->metadata = metadata;
                state_->beginThread = std::this_thread::get_id();
                ++state_->beginCount;
                return state_->failBegin ? psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed)
                                         : psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Consume(const NrDiagnosticRecord& record) noexcept override
            {
                state_->lastRecord = record;
                state_->consumeThread = std::this_thread::get_id();
                try
                {
                    state_->records.push_back(record);
                }
                catch (const std::bad_alloc&)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::OutOfMemory);
                }
                ++state_->consumeCount;
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Finish(const NrDiagnosticSummary& summary) noexcept override
            {
                state_->summary = summary;
                state_->finishThread = std::this_thread::get_id();
                ++state_->finishCount;
                return state_->failFinish ? psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed)
                                         : psnr::core::NrStatus::Success();
            }

        private:
            std::shared_ptr<NrCapturingDiagnosticSinkState> state_;
        };

        struct NrBlockingDiagnosticSinkState final
        {
            [[nodiscard]] bool WaitUntilConsumeEntered() noexcept
            {
                std::unique_lock<std::mutex> lock(mutex);
                return condition.wait_for(lock, std::chrono::seconds(2), [this]() noexcept { return consumeEntered; });
            }

            void ReleaseConsume() noexcept
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    consumeReleased = true;
                }
                condition.notify_one();
            }

            std::mutex mutex;
            std::condition_variable condition;
            bool consumeEntered = false;
            bool consumeReleased = false;
            bool failConsume = false;
            std::uint32_t consumeCount = 0;
            std::uint32_t finishCount = 0;
        };

        class NrBlockingDiagnosticSink final : public INrDiagnosticSink
        {
        public:
            explicit NrBlockingDiagnosticSink(std::shared_ptr<NrBlockingDiagnosticSinkState> state) noexcept
                : state_(std::move(state))
            {
            }

            [[nodiscard]] psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Consume(const NrDiagnosticRecord&) noexcept override
            {
                std::unique_lock<std::mutex> lock(state_->mutex);
                ++state_->consumeCount;
                state_->consumeEntered = true;
                state_->condition.notify_one();
                state_->condition.wait(lock, [this]() noexcept { return state_->consumeReleased; });
                return state_->failConsume ? psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::IoFailed)
                                           : psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Finish(const NrDiagnosticSummary&) noexcept override
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                ++state_->finishCount;
                return psnr::core::NrStatus::Success();
            }

        private:
            std::shared_ptr<NrBlockingDiagnosticSinkState> state_;
        };

        class NrScriptedLifecycleComponent final : public INrServerLifecycleComponent
        {
        public:
            [[nodiscard]] NrStatus Configure(NrBootstrapContext&) noexcept override
            {
                ++configureCount;
                return configureStatus;
            }

            [[nodiscard]] NrStatus Start() noexcept override
            {
                ++startCount;
                return startStatus;
            }

            [[nodiscard]] NrStatus RequestStop(const NrStopContext&) noexcept override
            {
                ++requestStopCount;
                return requestStopStatus;
            }

            [[nodiscard]] NrStatus Shutdown() noexcept override
            {
                ++shutdownCount;
                return shutdownStatus;
            }

            NrStatus configureStatus = NrStatus::Success();
            NrStatus startStatus = NrStatus::Success();
            NrStatus requestStopStatus = NrStatus::Success();
            NrStatus shutdownStatus = NrStatus::Success();
            std::uint32_t configureCount = 0;
            std::uint32_t startCount = 0;
            std::uint32_t requestStopCount = 0;
            std::uint32_t shutdownCount = 0;
        };

        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateDiagnosticsMemoryPoolManager()
        {
            const psnr::core::NrResult<std::size_t> storageBytesResult =
                NrDiagnosticsQueue::RequiredStorageBytes(NrDiagnosticsConfigInternal::QueueCapacity);
            if (storageBytesResult.Failed())
            {
                ADD_FAILURE() << "failed to calculate diagnostics queue storage";
                return nullptr;
            }

            psnr::core::NrMemoryPoolManagerConfig managerConfig;
            managerConfig.pools = {
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                    psnr::core::NrMemoryPoolConfig{
                        storageBytesResult.Value(),
                        1,
                        psnr::core::NrCacheLineSize,
                    },
                },
            };

            psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
                psnr::core::NrMemoryPoolManager::Create(managerConfig);
            if (managerResult.Failed())
            {
                ADD_FAILURE() << "failed to create diagnostics memory pool manager";
                return nullptr;
            }

            return managerResult.TakeValue();
        }

        [[nodiscard]] std::unique_ptr<NrDiagnosticsComponent> CreateStartedDebugComponent(
            psnr::core::NrMemoryPoolManager& manager, std::unique_ptr<INrDiagnosticSink> sink)
        {
            const NrDiagnosticsConfigInternal config{NrDiagnosticsMode::Debug, {}};
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> componentResult =
                NrDiagnosticsComponent::Create(config, manager, std::move(sink));
            if (componentResult.Failed())
            {
                ADD_FAILURE() << "failed to create diagnostics component";
                return nullptr;
            }

            std::unique_ptr<NrDiagnosticsComponent> component = componentResult.TakeValue();
            NrBootstrapContext bootstrapContext;
            if (component->Configure(bootstrapContext).Failed() || component->Start().Failed())
            {
                ADD_FAILURE() << "failed to start diagnostics component";
                return nullptr;
            }

            return component;
        }

        void ExpectAccountingInvariants(const NrDiagnosticsStats& stats) noexcept
        {
            EXPECT_EQ(stats.attempted, stats.enqueued + stats.droppedQueueFull + stats.droppedSinkUnavailable);
            EXPECT_EQ(stats.enqueued, stats.consumed + stats.discardedAfterSinkFailure);
        }

        [[nodiscard]] bool WaitUntilSinkFailed(const NrDiagnosticsComponent& component) noexcept
        {
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (component.CaptureStats().sinkFailed)
                {
                    return true;
                }
                std::this_thread::yield();
            }

            return false;
        }

        [[nodiscard]] NrServerConfig CreateGraphTestServerConfig() noexcept
        {
            NrServerConfig config;
            config.bindEndpoint.port = 1;
            config.actorMailboxCapacity = 2;
            config.pendingSendQueueCapacity = 2;
            config.maxSessionCount = 1;
            config.toWorldEventCapacity = 2;
            return config;
        }

        [[nodiscard]] bool ContainsDiagnosticsComponent(
            const std::span<INrServerLifecycleComponent* const> components) noexcept
        {
            for (INrServerLifecycleComponent* component : components)
            {
                if (dynamic_cast<NrDiagnosticsComponent*>(component) != nullptr)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] NrUtf8View MakeUtf8View(const std::string& value) noexcept
        {
            return NrUtf8View{value.data(), static_cast<std::uint32_t>(value.size())};
        }

        TEST(NrDiagnosticsConfigInternalTests, CreatesOwnedBenchmarkPathCopy)
        {
            std::string callerPath = "artifacts/diagnostics.jsonl";
            const std::string expectedPath = callerPath;
            const NrDiagnosticsConfig config{NrDiagnosticsMode::Benchmark, MakeUtf8View(callerPath)};

            psnr::core::NrResult<NrDiagnosticsConfigInternal> result = CreateDiagnosticsConfigInternal(config);
            ASSERT_TRUE(result.Succeeded());

            callerPath.assign("caller storage was replaced");

            EXPECT_EQ(result.Value().mode, NrDiagnosticsMode::Benchmark);
            EXPECT_EQ(result.Value().outputPath, expectedPath);
        }

        TEST(NrDiagnosticsConfigInternalTests, AcceptsValidMultibyteUtf8Path)
        {
            const char utf8Path[] = {'l',
                                     'o',
                                     'g',
                                     's',
                                     '/',
                                     static_cast<char>(0xEC),
                                     static_cast<char>(0xA7),
                                     static_cast<char>(0x84),
                                     static_cast<char>(0xEB),
                                     static_cast<char>(0x8B),
                                     static_cast<char>(0xA8),
                                     '.',
                                     'j',
                                     's',
                                     'o',
                                     'n',
                                     'l'};
            const NrDiagnosticsConfig config{
                NrDiagnosticsMode::Benchmark,
                NrUtf8View{utf8Path, static_cast<std::uint32_t>(sizeof(utf8Path))},
            };

            psnr::core::NrResult<NrDiagnosticsConfigInternal> result = CreateDiagnosticsConfigInternal(config);

            ASSERT_TRUE(result.Succeeded());
            EXPECT_EQ(result.Value().outputPath.size(), sizeof(utf8Path));
        }

        TEST(NrDiagnosticRecordTests, KeepsFixedEnumStorageAndIds)
        {
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticSeverity>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticEventKind>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticComponent>, std::uint16_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticOperation>, std::uint16_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticContextFlags>, std::uint8_t>));
            EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NrDiagnosticIoOperation>, std::uint8_t>));

            EXPECT_EQ(static_cast<std::uint8_t>(NrDiagnosticSeverity::Error), 3u);
            EXPECT_EQ(static_cast<std::uint8_t>(NrDiagnosticEventKind::Anomaly), 3u);
            EXPECT_EQ(static_cast<std::uint16_t>(NrDiagnosticComponent::MemoryPool), 6u);
            EXPECT_EQ(static_cast<std::uint16_t>(NrDiagnosticOperation::Acquire), 10u);
            EXPECT_EQ(static_cast<std::uint16_t>(NrDiagnosticOperation::Post), 11u);
            EXPECT_EQ(static_cast<std::uint16_t>(NrDiagnosticOperation::Complete), 12u);
            EXPECT_EQ(static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey), 1u);
            EXPECT_EQ(static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation), 2u);
            EXPECT_EQ(static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasCloseReason), 4u);
            EXPECT_EQ(static_cast<std::uint8_t>(NrDiagnosticIoOperation::Send), 3u);
        }

        TEST(NrDiagnosticRecordTests, KeepsFortyEightByteTriviallyCopyableLayout)
        {
            EXPECT_EQ(sizeof(NrDiagnosticRecord), 48u);
            EXPECT_EQ(alignof(NrDiagnosticRecord), alignof(std::uint64_t));
            EXPECT_TRUE(psnr::core::NrConceptTriviallyCopyable<NrDiagnosticRecord>);
        }

        TEST(NrDiagnosticRecordTests, DefaultsToUnassignedContext)
        {
            const NrDiagnosticRecord record;

            EXPECT_EQ(record.producerTimestamp, 0u);
            EXPECT_EQ(record.drainSequence, 0u);
            EXPECT_EQ(record.sessionKey, 0u);
            EXPECT_EQ(record.errorCode, psnr::core::NrErrorCode::Success);
            EXPECT_EQ(record.nativeErrorCode, 0u);
            EXPECT_EQ(record.component, NrDiagnosticComponent::Unknown);
            EXPECT_EQ(record.operation, NrDiagnosticOperation::Unknown);
            EXPECT_EQ(record.severity, NrDiagnosticSeverity::Unknown);
            EXPECT_EQ(record.eventKind, NrDiagnosticEventKind::Unknown);
            EXPECT_EQ(record.contextFlags, NrDiagnosticContextFlags::None);
            EXPECT_EQ(record.ioOperation, NrDiagnosticIoOperation::Unknown);
            EXPECT_EQ(record.closeReason, NrSessionEndReason::None);
        }

        TEST(NrDiagnosticRecordTests, DifferentiatesSameStatusByOccurrenceContext)
        {
            NrDiagnosticRecord lifecycleFailure;
            lifecycleFailure.errorCode = psnr::core::NrErrorCode::InvalidState;
            lifecycleFailure.component = NrDiagnosticComponent::ServerLifecycle;
            lifecycleFailure.operation = NrDiagnosticOperation::Start;

            NrDiagnosticRecord sessionFailure;
            sessionFailure.errorCode = psnr::core::NrErrorCode::InvalidState;
            sessionFailure.component = NrDiagnosticComponent::Session;
            sessionFailure.operation = NrDiagnosticOperation::Close;

            EXPECT_EQ(lifecycleFailure.errorCode, sessionFailure.errorCode);
            EXPECT_NE(lifecycleFailure.component, sessionFailure.component);
            EXPECT_NE(lifecycleFailure.operation, sessionFailure.operation);
        }

        TEST(NrDiagnosticSinkTests, RunsBeginConsumeFinishLifecycleThroughInternalSeam)
        {
            std::shared_ptr<NrCapturingDiagnosticSinkState> capture =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<INrDiagnosticSink> sink = std::make_unique<NrCapturingDiagnosticSink>(capture);

            NrDiagnosticRecord record;
            record.component = NrDiagnosticComponent::Session;
            record.operation = NrDiagnosticOperation::Receive;
            const NrDiagnosticRunMetadata metadata{NrDiagnosticsMode::Debug};
            const NrDiagnosticSummary summary{1, 1, 0, 0, 1, 0};

            EXPECT_TRUE(sink->Begin(metadata).Succeeded());
            EXPECT_TRUE(sink->Consume(record).Succeeded());
            EXPECT_TRUE(sink->Finish(summary).Succeeded());
            EXPECT_EQ(capture->beginCount, 1u);
            EXPECT_EQ(capture->consumeCount, 1u);
            EXPECT_EQ(capture->finishCount, 1u);
            EXPECT_EQ(capture->metadata.mode, NrDiagnosticsMode::Debug);
            EXPECT_EQ(capture->summary.consumed, 1u);
            EXPECT_EQ(capture->lastRecord.component, record.component);
            EXPECT_EQ(capture->lastRecord.operation, record.operation);
        }

        TEST(NrDiagnosticsComponentTests, DisabledModeKeepsLifecycleAndEmitAsCompleteNoOp)
        {
            psnr::core::NrMemoryPoolManagerConfig managerConfig;
            managerConfig.pools = {
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    psnr::core::NrMemoryPoolRole::RecvBuffer,
                    psnr::core::NrMemoryPoolConfig{64, 1, psnr::core::NrCacheLineSize},
                },
            };
            psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
                psnr::core::NrMemoryPoolManager::Create(managerConfig);
            ASSERT_TRUE(managerResult.Succeeded());
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = managerResult.TakeValue();

            const NrDiagnosticsConfigInternal config;
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> componentResult =
                NrDiagnosticsComponent::Create(config, *manager, nullptr);
            ASSERT_TRUE(componentResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> component = componentResult.TakeValue();

            NrBootstrapContext bootstrapContext;
            EXPECT_TRUE(component->Configure(bootstrapContext).Succeeded());
            EXPECT_TRUE(component->Start().Succeeded());
            EXPECT_TRUE(component->RequestStop(NrStopContext{}).Succeeded());

            NrDiagnosticRecord record;
            record.component = NrDiagnosticComponent::ServerLifecycle;
            component->Emitter().Emit(record);
            EXPECT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_FALSE(stats.enabled);
            EXPECT_FALSE(stats.sinkFailed);
            EXPECT_EQ(stats.attempted, 0u);
            EXPECT_EQ(stats.enqueued, 0u);
            EXPECT_EQ(stats.droppedQueueFull, 0u);
            EXPECT_EQ(stats.droppedSinkUnavailable, 0u);
            EXPECT_EQ(stats.consumed, 0u);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 0u);

            const psnr::core::NrResult<psnr::core::NrMemoryPoolStats> diagnosticsPoolStats =
                manager->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            EXPECT_TRUE(diagnosticsPoolStats.Failed());
            EXPECT_EQ(diagnosticsPoolStats.Status().ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        }

        TEST(NrDiagnosticsComponentTests, RequestStopKeepsEmittingUntilShutdownDrainsAndFlushes)
        {
            const psnr::core::NrResult<std::size_t> storageBytesResult =
                NrDiagnosticsQueue::RequiredStorageBytes(NrDiagnosticsConfigInternal::QueueCapacity);
            ASSERT_TRUE(storageBytesResult.Succeeded());

            psnr::core::NrMemoryPoolManagerConfig managerConfig;
            managerConfig.pools = {
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                    psnr::core::NrMemoryPoolConfig{
                        storageBytesResult.Value(),
                        1,
                        psnr::core::NrCacheLineSize,
                    },
                },
            };
            psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
                psnr::core::NrMemoryPoolManager::Create(managerConfig);
            ASSERT_TRUE(managerResult.Succeeded());
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = managerResult.TakeValue();

            std::shared_ptr<NrCapturingDiagnosticSinkState> capture =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<INrDiagnosticSink> sink = std::make_unique<NrCapturingDiagnosticSink>(capture);
            const NrDiagnosticsConfigInternal config{NrDiagnosticsMode::Debug, {}};

            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> componentResult =
                NrDiagnosticsComponent::Create(config, *manager, std::move(sink));
            ASSERT_TRUE(componentResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> component = componentResult.TakeValue();

            NrBootstrapContext bootstrapContext;
            ASSERT_TRUE(component->Configure(bootstrapContext).Succeeded());
            ASSERT_TRUE(component->Start().Succeeded());

            NrDiagnosticRecord beforeStop;
            beforeStop.component = NrDiagnosticComponent::Session;
            beforeStop.operation = NrDiagnosticOperation::Receive;
            component->Emitter().Emit(beforeStop);

            ASSERT_TRUE(component->RequestStop(NrStopContext{}).Succeeded());

            NrDiagnosticRecord afterStop;
            afterStop.component = NrDiagnosticComponent::ActorScheduler;
            afterStop.operation = NrDiagnosticOperation::Admission;
            component->Emitter().Emit(afterStop);

            ASSERT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_TRUE(stats.enabled);
            EXPECT_FALSE(stats.sinkFailed);
            EXPECT_EQ(stats.attempted, 2u);
            EXPECT_EQ(stats.enqueued, 2u);
            EXPECT_EQ(stats.consumed, 2u);
            EXPECT_EQ(stats.droppedQueueFull, 0u);
            EXPECT_EQ(stats.droppedSinkUnavailable, 0u);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 0u);

            EXPECT_EQ(capture->consumeCount, 2u);
            EXPECT_EQ(capture->finishCount, 1u);
            EXPECT_EQ(capture->beginCount, 1u);
            EXPECT_EQ(capture->metadata.mode, NrDiagnosticsMode::Debug);
            EXPECT_EQ(capture->summary.attempted, stats.attempted);
            EXPECT_EQ(capture->summary.enqueued, stats.enqueued);
            EXPECT_EQ(capture->summary.consumed, stats.consumed);
            EXPECT_EQ(capture->beginThread, capture->consumeThread);
            EXPECT_EQ(capture->consumeThread, capture->finishThread);
            EXPECT_EQ(capture->lastRecord.component, afterStop.component);
            EXPECT_EQ(capture->lastRecord.operation, afterStop.operation);
            EXPECT_GT(capture->lastRecord.producerTimestamp, 0u);
            EXPECT_EQ(capture->lastRecord.drainSequence, 2u);

            const psnr::core::NrResult<psnr::core::NrMemoryPoolStats> diagnosticsPoolStats =
                manager->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(diagnosticsPoolStats.Succeeded());
            EXPECT_EQ(diagnosticsPoolStats.Value().inUse, 0u);
        }

        TEST(NrDiagnosticsComponentTests, BeginFailureLatchesWithoutChangingStartResultOrCallingSinkAgain)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            sinkState->failBegin = true;
            const NrDiagnosticsConfigInternal config{NrDiagnosticsMode::Debug, {}};
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> componentResult =
                NrDiagnosticsComponent::Create(
                    config, *manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_TRUE(componentResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> component = componentResult.TakeValue();

            NrBootstrapContext bootstrapContext;
            ASSERT_TRUE(component->Configure(bootstrapContext).Succeeded());
            EXPECT_TRUE(component->Start().Succeeded());
            EXPECT_FALSE(component->Emitter().IsEnabled());
            EXPECT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_TRUE(stats.sinkFailed);
            EXPECT_EQ(stats.attempted, 0u);
            EXPECT_EQ(stats.enqueued, 0u);
            EXPECT_EQ(stats.consumed, 0u);
            EXPECT_EQ(sinkState->beginCount, 1u);
            EXPECT_EQ(sinkState->consumeCount, 0u);
            EXPECT_EQ(sinkState->finishCount, 0u);
        }

        TEST(NrDiagnosticsComponentTests, QueueFullDropsOnlyNewestRecordAndPreservesAccounting)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrBlockingDiagnosticSinkState> sinkState =
                std::make_shared<NrBlockingDiagnosticSinkState>();
            std::unique_ptr<NrDiagnosticsComponent> component =
                CreateStartedDebugComponent(*manager, std::make_unique<NrBlockingDiagnosticSink>(sinkState));
            ASSERT_NE(component, nullptr);

            component->Emitter().Emit(NrDiagnosticRecord{});
            const bool consumeEntered = sinkState->WaitUntilConsumeEntered();
            if (!consumeEntered)
            {
                sinkState->ReleaseConsume();
            }
            ASSERT_TRUE(consumeEntered);

            for (std::size_t index = 0; index < NrDiagnosticsConfigInternal::QueueCapacity; ++index)
            {
                NrDiagnosticRecord record;
                record.sessionKey = static_cast<psnr::core::NrSessionKey>(index + 1);
                component->Emitter().Emit(record);
            }
            component->Emitter().Emit(NrDiagnosticRecord{});

            sinkState->ReleaseConsume();
            ASSERT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_EQ(stats.attempted, NrDiagnosticsConfigInternal::QueueCapacity + 2u);
            EXPECT_EQ(stats.enqueued, NrDiagnosticsConfigInternal::QueueCapacity + 1u);
            EXPECT_EQ(stats.droppedQueueFull, 1u);
            EXPECT_EQ(stats.droppedSinkUnavailable, 0u);
            EXPECT_EQ(stats.consumed, NrDiagnosticsConfigInternal::QueueCapacity + 1u);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 0u);
            ExpectAccountingInvariants(stats);

            EXPECT_EQ(sinkState->consumeCount, NrDiagnosticsConfigInternal::QueueCapacity + 1u);
            EXPECT_EQ(sinkState->finishCount, 1u);
        }

        TEST(NrDiagnosticsComponentTests, FirstConsumeFailureDisablesSinkAndSeparatesQueuedFromFutureDrops)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrBlockingDiagnosticSinkState> sinkState =
                std::make_shared<NrBlockingDiagnosticSinkState>();
            sinkState->failConsume = true;
            std::unique_ptr<NrDiagnosticsComponent> component =
                CreateStartedDebugComponent(*manager, std::make_unique<NrBlockingDiagnosticSink>(sinkState));
            ASSERT_NE(component, nullptr);

            component->Emitter().Emit(NrDiagnosticRecord{});
            const bool consumeEntered = sinkState->WaitUntilConsumeEntered();
            if (!consumeEntered)
            {
                sinkState->ReleaseConsume();
            }
            ASSERT_TRUE(consumeEntered);

            constexpr std::size_t QueuedAfterFailingRecord = 3;
            for (std::size_t index = 0; index < QueuedAfterFailingRecord; ++index)
            {
                component->Emitter().Emit(NrDiagnosticRecord{});
            }

            sinkState->ReleaseConsume();
            ASSERT_TRUE(WaitUntilSinkFailed(*component));

            constexpr std::size_t FutureEmits = 2;
            for (std::size_t index = 0; index < FutureEmits; ++index)
            {
                component->Emitter().Emit(NrDiagnosticRecord{});
            }

            ASSERT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_TRUE(stats.sinkFailed);
            EXPECT_EQ(stats.attempted, 1u + QueuedAfterFailingRecord + FutureEmits);
            EXPECT_EQ(stats.enqueued, 1u + QueuedAfterFailingRecord);
            EXPECT_EQ(stats.droppedQueueFull, 0u);
            EXPECT_EQ(stats.droppedSinkUnavailable, FutureEmits);
            EXPECT_EQ(stats.consumed, 0u);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 1u + QueuedAfterFailingRecord);
            ExpectAccountingInvariants(stats);

            EXPECT_EQ(sinkState->consumeCount, 1u);
            EXPECT_EQ(sinkState->finishCount, 0u);
        }

        TEST(NrDiagnosticsComponentTests, FinishFailureLatchesWithoutDiscardingConsumedRecords)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            sinkState->failFinish = true;
            std::unique_ptr<NrDiagnosticsComponent> component =
                CreateStartedDebugComponent(*manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_NE(component, nullptr);

            component->Emitter().Emit(NrDiagnosticRecord{});
            ASSERT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_TRUE(stats.sinkFailed);
            EXPECT_EQ(stats.attempted, 1u);
            EXPECT_EQ(stats.enqueued, 1u);
            EXPECT_EQ(stats.consumed, 1u);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 0u);
            ExpectAccountingInvariants(stats);

            EXPECT_EQ(sinkState->consumeCount, 1u);
            EXPECT_EQ(sinkState->finishCount, 1u);
        }

        TEST(NrDiagnosticsComponentTests, NewComponentResetsFailureLatchAndCounters)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrCapturingDiagnosticSinkState> failingSinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            failingSinkState->failFinish = true;
            std::unique_ptr<NrDiagnosticsComponent> failedComponent =
                CreateStartedDebugComponent(*manager, std::make_unique<NrCapturingDiagnosticSink>(failingSinkState));
            ASSERT_NE(failedComponent, nullptr);
            failedComponent->Emitter().Emit(NrDiagnosticRecord{});
            ASSERT_TRUE(failedComponent->Shutdown().Succeeded());
            ASSERT_TRUE(failedComponent->CaptureStats().sinkFailed);

            std::shared_ptr<NrCapturingDiagnosticSinkState> healthySinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<NrDiagnosticsComponent> healthyComponent =
                CreateStartedDebugComponent(*manager, std::make_unique<NrCapturingDiagnosticSink>(healthySinkState));
            ASSERT_NE(healthyComponent, nullptr);

            const NrDiagnosticsStats initialStats = healthyComponent->CaptureStats();
            EXPECT_TRUE(initialStats.enabled);
            EXPECT_FALSE(initialStats.sinkFailed);
            EXPECT_EQ(initialStats.attempted, 0u);
            EXPECT_EQ(initialStats.enqueued, 0u);
            EXPECT_EQ(initialStats.droppedQueueFull, 0u);
            EXPECT_EQ(initialStats.droppedSinkUnavailable, 0u);
            EXPECT_EQ(initialStats.consumed, 0u);
            EXPECT_EQ(initialStats.discardedAfterSinkFailure, 0u);

            healthyComponent->Emitter().Emit(NrDiagnosticRecord{});
            ASSERT_TRUE(healthyComponent->Shutdown().Succeeded());

            const NrDiagnosticsStats finalStats = healthyComponent->CaptureStats();
            EXPECT_FALSE(finalStats.sinkFailed);
            EXPECT_EQ(finalStats.attempted, 1u);
            EXPECT_EQ(finalStats.enqueued, 1u);
            EXPECT_EQ(finalStats.consumed, 1u);
            ExpectAccountingInvariants(finalStats);
        }

        TEST(NrDiagnosticsComponentTests, MultipleProducersPreserveUniqueRecordsAndConsumerDrainSequence)
        {
            constexpr std::size_t ProducerCount = 4;
            constexpr std::size_t RecordsPerProducer = 128;
            constexpr std::size_t TotalRecords = ProducerCount * RecordsPerProducer;

            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);

            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            sinkState->records.reserve(TotalRecords);
            std::unique_ptr<NrDiagnosticsComponent> component =
                CreateStartedDebugComponent(*manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_NE(component, nullptr);
            const NrDiagnosticEmitter emitter = component->Emitter();

            std::atomic_bool startProducers{false};
            std::vector<std::thread> producers;
            producers.reserve(ProducerCount);
            for (std::size_t producerIndex = 0; producerIndex < ProducerCount; ++producerIndex)
            {
                producers.emplace_back(
                    [emitter, &startProducers, producerIndex]() noexcept
                    {
                        startProducers.wait(false, std::memory_order_acquire);
                        for (std::size_t recordIndex = 0; recordIndex < RecordsPerProducer; ++recordIndex)
                        {
                            NrDiagnosticRecord record;
                            record.sessionKey = static_cast<psnr::core::NrSessionKey>(
                                producerIndex * RecordsPerProducer + recordIndex + 1);
                            emitter.Emit(record);
                        }
                    });
            }

            startProducers.store(true, std::memory_order_release);
            startProducers.notify_all();
            for (std::thread& producer : producers)
            {
                producer.join();
            }

            ASSERT_TRUE(component->Shutdown().Succeeded());

            const NrDiagnosticsStats stats = component->CaptureStats();
            EXPECT_EQ(stats.attempted, TotalRecords);
            EXPECT_EQ(stats.enqueued, TotalRecords);
            EXPECT_EQ(stats.droppedQueueFull, 0u);
            EXPECT_EQ(stats.droppedSinkUnavailable, 0u);
            EXPECT_EQ(stats.consumed, TotalRecords);
            EXPECT_EQ(stats.discardedAfterSinkFailure, 0u);
            ExpectAccountingInvariants(stats);

            ASSERT_EQ(sinkState->records.size(), TotalRecords);
            std::vector<bool> seenSessionKeys(TotalRecords, false);
            for (std::size_t index = 0; index < sinkState->records.size(); ++index)
            {
                const NrDiagnosticRecord& record = sinkState->records[index];
                EXPECT_EQ(record.drainSequence, index + 1u);
                ASSERT_GT(record.sessionKey, 0u);
                ASSERT_LE(record.sessionKey, TotalRecords);

                const std::size_t keyIndex = static_cast<std::size_t>(record.sessionKey - 1);
                EXPECT_FALSE(seenSessionKeys[keyIndex]);
                seenSessionKeys[keyIndex] = true;
            }
        }

        TEST(NrDiagnosticsGraphTests, InjectedSinkPlacesDiagnosticsFirstInForwardLifecycleOrder)
        {
            NrServerConfig serverConfig = CreateGraphTestServerConfig();
            serverConfig.diagnostics.mode = NrDiagnosticsMode::Debug;
            const NrDiagnosticsConfigInternal diagnosticsConfig{NrDiagnosticsMode::Debug, {}};
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();

            NrServerComponentGraph graph;
            const NrStatus buildStatus = BuildServerGraph(serverConfig, diagnosticsConfig, graph,
                                                          std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_TRUE(buildStatus.Succeeded());

            const std::span<INrServerLifecycleComponent* const> lifecycleOrder = graph.LifecycleOrder();
            ASSERT_FALSE(lifecycleOrder.empty());
            EXPECT_NE(dynamic_cast<NrDiagnosticsComponent*>(lifecycleOrder.front()), nullptr);

            const psnr::core::NrResult<psnr::core::NrMemoryPoolStats> poolStats =
                graph.MemoryPoolManager()->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(poolStats.Succeeded());
            EXPECT_EQ(poolStats.Value().capacity, 1u);
            EXPECT_EQ(poolStats.Value().inUse, 1u);
        }

        TEST(NrDiagnosticsGraphTests, ProductionGraphCreatesDebugDiagnosticsComponent)
        {
            NrServerConfig serverConfig = CreateGraphTestServerConfig();
            serverConfig.diagnostics.mode = NrDiagnosticsMode::Debug;
            const NrDiagnosticsConfigInternal diagnosticsConfig{NrDiagnosticsMode::Debug, {}};

            NrServerComponentGraph graph;
            ASSERT_TRUE(BuildServerGraph(serverConfig, diagnosticsConfig, graph).Succeeded());

            const std::span<INrServerLifecycleComponent* const> lifecycleOrder = graph.LifecycleOrder();
            ASSERT_FALSE(lifecycleOrder.empty());
            EXPECT_NE(dynamic_cast<NrDiagnosticsComponent*>(lifecycleOrder.front()), nullptr);
            EXPECT_TRUE(graph.CaptureDiagnosticsStats().enabled);
            EXPECT_FALSE(graph.CaptureDiagnosticsStats().sinkFailed);
            EXPECT_FALSE(graph.DiagnosticsEmitter().IsEnabled());
            const psnr::core::NrResult<psnr::core::NrMemoryPoolStats> poolStats =
                graph.MemoryPoolManager()->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(poolStats.Succeeded());
            EXPECT_EQ(poolStats.Value().capacity, 1u);
            EXPECT_EQ(poolStats.Value().inUse, 1u);
        }

        TEST(NrDiagnosticsGraphTests, ProductionGraphCreatesBenchmarkDiagnosticsComponent)
        {
            std::string outputPath = "artifacts/diagnostics.jsonl";
            NrServerConfig serverConfig = CreateGraphTestServerConfig();
            serverConfig.diagnostics =
                NrDiagnosticsConfig{NrDiagnosticsMode::Benchmark, MakeUtf8View(outputPath)};
            const NrDiagnosticsConfigInternal diagnosticsConfig{NrDiagnosticsMode::Benchmark, outputPath};

            NrServerComponentGraph graph;
            ASSERT_TRUE(BuildServerGraph(serverConfig, diagnosticsConfig, graph).Succeeded());

            EXPECT_TRUE(ContainsDiagnosticsComponent(graph.LifecycleOrder()));
            EXPECT_TRUE(graph.CaptureDiagnosticsStats().enabled);
            EXPECT_FALSE(graph.CaptureDiagnosticsStats().sinkFailed);
            EXPECT_FALSE(graph.DiagnosticsEmitter().IsEnabled());
        }

        TEST(NrDiagnosticsGraphTests, DisabledProductionGraphDoesNotConfigureDiagnosticsPoolOrComponent)
        {
            const NrServerConfig serverConfig = CreateGraphTestServerConfig();
            const NrDiagnosticsConfigInternal diagnosticsConfig;

            NrServerComponentGraph graph;
            ASSERT_TRUE(BuildServerGraph(serverConfig, diagnosticsConfig, graph).Succeeded());

            EXPECT_FALSE(ContainsDiagnosticsComponent(graph.LifecycleOrder()));
            EXPECT_FALSE(graph.CaptureDiagnosticsStats().enabled);
            EXPECT_FALSE(graph.CaptureDiagnosticsStats().sinkFailed);
            EXPECT_FALSE(graph.DiagnosticsEmitter().IsEnabled());
            const psnr::core::NrResult<psnr::core::NrMemoryPoolStats> poolStats =
                graph.MemoryPoolManager()->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            EXPECT_TRUE(poolStats.Failed());
            EXPECT_EQ(poolStats.Status().ErrorCode(), psnr::core::NrErrorCode::InvalidState);
        }

        TEST(NrBootstrapPlanDiagnosticsTests, StartAndRollbackFailuresAreBothRecordedWhileFirstFailureIsReturned)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();

            const NrDiagnosticsConfigInternal diagnosticsConfig{NrDiagnosticsMode::Debug, {}};
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> diagnosticsResult =
                NrDiagnosticsComponent::Create(diagnosticsConfig, *manager,
                                               std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_TRUE(diagnosticsResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = diagnosticsResult.TakeValue();

            NrScriptedLifecycleComponent startedComponent;
            startedComponent.shutdownStatus =
                NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 2002);
            NrScriptedLifecycleComponent failingStartComponent;
            failingStartComponent.startStatus =
                NrStatus::Failure(psnr::core::NrErrorCode::ProtocolError, 1001);

            const std::array<INrServerLifecycleComponent*, 3> components{
                diagnostics.get(),
                &startedComponent,
                &failingStartComponent,
            };
            NrBootstrapPlan plan(NrBootstrapPlanInput{components, diagnostics->Emitter()});
            NrBootstrapContext context;
            ASSERT_TRUE(plan.Configure(context).Succeeded());

            const NrStatus startStatus = plan.Start();
            EXPECT_EQ(startStatus.ErrorCode(), psnr::core::NrErrorCode::ProtocolError);
            EXPECT_EQ(startStatus.NativeErrorCode(), 1001u);
            EXPECT_EQ(startedComponent.shutdownCount, 1u);
            EXPECT_EQ(failingStartComponent.shutdownCount, 0u);

            ASSERT_EQ(sinkState->records.size(), 2u);
            EXPECT_EQ(sinkState->records[0].severity, NrDiagnosticSeverity::Error);
            EXPECT_EQ(sinkState->records[0].eventKind, NrDiagnosticEventKind::Failure);
            EXPECT_EQ(sinkState->records[0].component, NrDiagnosticComponent::ServerLifecycle);
            EXPECT_EQ(sinkState->records[0].operation, NrDiagnosticOperation::Start);
            EXPECT_EQ(sinkState->records[0].errorCode, psnr::core::NrErrorCode::ProtocolError);
            EXPECT_EQ(sinkState->records[0].nativeErrorCode, 1001u);
            EXPECT_EQ(sinkState->records[1].component, NrDiagnosticComponent::ServerLifecycle);
            EXPECT_EQ(sinkState->records[1].operation, NrDiagnosticOperation::Shutdown);
            EXPECT_EQ(sinkState->records[1].errorCode, psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(sinkState->records[1].nativeErrorCode, 2002u);
        }

        TEST(NrBootstrapPlanDiagnosticsTests, StopAndShutdownRecordEveryFailureWhileReturningFirstFailure)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();

            const NrDiagnosticsConfigInternal diagnosticsConfig{NrDiagnosticsMode::Debug, {}};
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> diagnosticsResult =
                NrDiagnosticsComponent::Create(diagnosticsConfig, *manager,
                                               std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_TRUE(diagnosticsResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = diagnosticsResult.TakeValue();

            NrScriptedLifecycleComponent firstComponent;
            firstComponent.requestStopStatus =
                NrStatus::Failure(psnr::core::NrErrorCode::ProtocolError, 12);
            firstComponent.shutdownStatus =
                NrStatus::Failure(psnr::core::NrErrorCode::OperationCanceled, 22);
            NrScriptedLifecycleComponent secondComponent;
            secondComponent.requestStopStatus =
                NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 11);
            secondComponent.shutdownStatus =
                NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 21);

            const std::array<INrServerLifecycleComponent*, 3> components{
                diagnostics.get(),
                &firstComponent,
                &secondComponent,
            };
            NrBootstrapPlan plan(NrBootstrapPlanInput{components, diagnostics->Emitter()});
            NrBootstrapContext context;
            ASSERT_TRUE(plan.Configure(context).Succeeded());
            ASSERT_TRUE(plan.Start().Succeeded());

            const NrStatus requestStopStatus =
                plan.RequestStop(NrStopContext{NrStopReason::Requested, NrStopMode::Graceful});
            EXPECT_EQ(requestStopStatus.ErrorCode(), psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(requestStopStatus.NativeErrorCode(), 11u);

            const NrStatus shutdownStatus = plan.Shutdown();
            EXPECT_EQ(shutdownStatus.ErrorCode(), psnr::core::NrErrorCode::IoFailed);
            EXPECT_EQ(shutdownStatus.NativeErrorCode(), 21u);

            ASSERT_EQ(sinkState->records.size(), 4u);
            EXPECT_EQ(sinkState->records[0].operation, NrDiagnosticOperation::RequestStop);
            EXPECT_EQ(sinkState->records[0].nativeErrorCode, 11u);
            EXPECT_EQ(sinkState->records[1].operation, NrDiagnosticOperation::RequestStop);
            EXPECT_EQ(sinkState->records[1].nativeErrorCode, 12u);
            EXPECT_EQ(sinkState->records[2].operation, NrDiagnosticOperation::Shutdown);
            EXPECT_EQ(sinkState->records[2].nativeErrorCode, 21u);
            EXPECT_EQ(sinkState->records[3].operation, NrDiagnosticOperation::Shutdown);
            EXPECT_EQ(sinkState->records[3].nativeErrorCode, 22u);
        }

        TEST(NrIoDiagnosticsTests, CompletionFailuresPreserveOperationStatusAndSessionContext)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = CreateStartedDebugComponent(
                *manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_NE(diagnostics, nullptr);

            NrIoEventDispatcherDependencies dependencies;
            dependencies.diagnosticsEmitter = diagnostics->Emitter();
            NrIoEventDispatcher dispatcher(dependencies);

            NrAcceptIoContext acceptContext;
            std::array<std::byte, 1> recvBuffer{};
            NrRecvIoContext recvContext(17, recvBuffer);
            NrSendIoContext sendContext(18, psnr::core::NrPayloadRef{});

            const NrStatus acceptFailure =
                NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 1001);
            const NrStatus recvFailure =
                NrStatus::Failure(psnr::core::NrErrorCode::OperationCanceled, 1002);
            const NrStatus sendFailure =
                NrStatus::Failure(psnr::core::NrErrorCode::IoFailed, 1003);

            EXPECT_EQ(dispatcher.DispatchAccept(NrIoEvent::Accept(0, acceptFailure), acceptContext).ErrorCode(),
                      acceptFailure.ErrorCode());
            EXPECT_TRUE(dispatcher.DispatchRecv(NrIoEvent::Recv(17, 1, 0, recvFailure), recvContext).Failed());
            EXPECT_TRUE(dispatcher.DispatchSend(NrIoEvent::Send(18, 1, 0, sendFailure), sendContext).Failed());

            ASSERT_TRUE(diagnostics->Shutdown().Succeeded());

            ASSERT_EQ(sinkState->records.size(), 3u);
            EXPECT_EQ(sinkState->records[0].component, NrDiagnosticComponent::IoPipeline);
            EXPECT_EQ(sinkState->records[0].operation, NrDiagnosticOperation::Complete);
            EXPECT_EQ(sinkState->records[0].ioOperation, NrDiagnosticIoOperation::Accept);
            EXPECT_EQ(sinkState->records[0].errorCode, acceptFailure.ErrorCode());
            EXPECT_EQ(sinkState->records[0].nativeErrorCode, acceptFailure.NativeErrorCode());
            EXPECT_EQ(sinkState->records[0].contextFlags, NrDiagnosticContextFlags::HasIoOperation);

            EXPECT_EQ(sinkState->records[1].ioOperation, NrDiagnosticIoOperation::Receive);
            EXPECT_EQ(sinkState->records[1].errorCode, recvFailure.ErrorCode());
            EXPECT_EQ(sinkState->records[1].nativeErrorCode, recvFailure.NativeErrorCode());
            EXPECT_EQ(sinkState->records[1].sessionKey, 17u);
            EXPECT_EQ(sinkState->records[1].contextFlags,
                      static_cast<NrDiagnosticContextFlags>(
                          static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation) |
                          static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey)));

            EXPECT_EQ(sinkState->records[2].ioOperation, NrDiagnosticIoOperation::Send);
            EXPECT_EQ(sinkState->records[2].errorCode, sendFailure.ErrorCode());
            EXPECT_EQ(sinkState->records[2].nativeErrorCode, sendFailure.NativeErrorCode());
            EXPECT_EQ(sinkState->records[2].sessionKey, 18u);
            EXPECT_EQ(sinkState->records[2].contextFlags,
                      static_cast<NrDiagnosticContextFlags>(
                          static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasIoOperation) |
                          static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey)));
        }

        TEST(NrIoDiagnosticsTests, AcceptPostFailureIsRecordedWithoutChangingReturnedStatus)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = CreateStartedDebugComponent(
                *manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_NE(diagnostics, nullptr);

            NrListener listener(NrListenerConfig{NrEndpoint{}, 1, 1},
                                NrListenerDependencies{nullptr, diagnostics->Emitter()});
            NrAcceptIoContext acceptContext;
            const NrStatus postStatus = listener.PostAccept(acceptContext);
            ASSERT_TRUE(postStatus.Failed());

            ASSERT_TRUE(diagnostics->Shutdown().Succeeded());

            ASSERT_EQ(sinkState->records.size(), 1u);
            EXPECT_EQ(sinkState->records[0].component, NrDiagnosticComponent::IoPipeline);
            EXPECT_EQ(sinkState->records[0].operation, NrDiagnosticOperation::Post);
            EXPECT_EQ(sinkState->records[0].severity, NrDiagnosticSeverity::Error);
            EXPECT_EQ(sinkState->records[0].eventKind, NrDiagnosticEventKind::Failure);
            EXPECT_EQ(sinkState->records[0].ioOperation, NrDiagnosticIoOperation::Accept);
            EXPECT_EQ(sinkState->records[0].errorCode, postStatus.ErrorCode());
            EXPECT_EQ(sinkState->records[0].nativeErrorCode, postStatus.NativeErrorCode());
            EXPECT_EQ(sinkState->records[0].contextFlags, NrDiagnosticContextFlags::HasIoOperation);
        }

        TEST(NrSchedulerDiagnosticsTests, ReadyQueueInvariantFailureIsRecordedAsAdmissionAnomaly)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = CreateStartedDebugComponent(
                *manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_NE(diagnostics, nullptr);

            NrSessionActorRegistry registry(1);
            NrServerMetrics metrics;
            NrSessionActorScheduler scheduler(registry, *manager, metrics, NrSessionActorSchedulerConfig{},
                                              diagnostics->Emitter());
            const NrStatus invariantFailure =
                NrStatus::Failure(psnr::core::NrErrorCode::QueueFull, 3001);

            NrSessionActorSchedulerTestAccess::EmitAdmissionAnomaly(scheduler, 71, invariantFailure);
            ASSERT_TRUE(diagnostics->Shutdown().Succeeded());

            ASSERT_EQ(sinkState->records.size(), 1u);
            EXPECT_EQ(sinkState->records[0].component, NrDiagnosticComponent::ActorScheduler);
            EXPECT_EQ(sinkState->records[0].operation, NrDiagnosticOperation::Admission);
            EXPECT_EQ(sinkState->records[0].severity, NrDiagnosticSeverity::Error);
            EXPECT_EQ(sinkState->records[0].eventKind, NrDiagnosticEventKind::Anomaly);
            EXPECT_EQ(sinkState->records[0].errorCode, invariantFailure.ErrorCode());
            EXPECT_EQ(sinkState->records[0].nativeErrorCode, invariantFailure.NativeErrorCode());
            EXPECT_EQ(sinkState->records[0].contextFlags, NrDiagnosticContextFlags::HasSessionKey);
            EXPECT_EQ(sinkState->records[0].sessionKey, 71u);
        }

        TEST(NrSessionBootstrapDiagnosticsTests, ResourceFailureIsRecordedOnceAtSessionBootstrapBoundary)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = CreateDiagnosticsMemoryPoolManager();
            ASSERT_NE(manager, nullptr);
            std::shared_ptr<NrCapturingDiagnosticSinkState> sinkState =
                std::make_shared<NrCapturingDiagnosticSinkState>();
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = CreateStartedDebugComponent(
                *manager, std::make_unique<NrCapturingDiagnosticSink>(sinkState));
            ASSERT_NE(diagnostics, nullptr);

            NrIoEventDispatcherDependencies dependencies;
            dependencies.diagnosticsEmitter = diagnostics->Emitter();
            NrIoEventDispatcher dispatcher(dependencies);
            const NrStatus poolFailure = NrStatus::Failure(psnr::core::NrErrorCode::PoolExhausted, 4001);
            const NrStatus admissionFailure = NrStatus::Failure(psnr::core::NrErrorCode::QueueFull, 4002);

            const NrStatus recordedPoolFailure =
                NrIoEventDispatcherTestAccess::RecordSessionBootstrapFailure(dispatcher, 81, poolFailure);
            EXPECT_EQ(recordedPoolFailure.ErrorCode(), poolFailure.ErrorCode());
            EXPECT_EQ(recordedPoolFailure.NativeErrorCode(), poolFailure.NativeErrorCode());

            const NrStatus recordedAdmissionFailure =
                NrIoEventDispatcherTestAccess::RecordSessionBootstrapFailure(dispatcher, 82, admissionFailure);
            EXPECT_EQ(recordedAdmissionFailure.ErrorCode(), admissionFailure.ErrorCode());
            EXPECT_EQ(recordedAdmissionFailure.NativeErrorCode(), admissionFailure.NativeErrorCode());
            ASSERT_TRUE(diagnostics->Shutdown().Succeeded());

            ASSERT_EQ(sinkState->records.size(), 2u);
            EXPECT_EQ(sinkState->records[0].component, NrDiagnosticComponent::MemoryPool);
            EXPECT_EQ(sinkState->records[0].operation, NrDiagnosticOperation::Acquire);
            EXPECT_EQ(sinkState->records[0].eventKind, NrDiagnosticEventKind::Failure);
            EXPECT_EQ(sinkState->records[0].errorCode, poolFailure.ErrorCode());
            EXPECT_EQ(sinkState->records[0].nativeErrorCode, poolFailure.NativeErrorCode());
            EXPECT_EQ(sinkState->records[0].contextFlags, NrDiagnosticContextFlags::HasSessionKey);
            EXPECT_EQ(sinkState->records[0].sessionKey, 81u);

            EXPECT_EQ(sinkState->records[1].component, NrDiagnosticComponent::Session);
            EXPECT_EQ(sinkState->records[1].operation, NrDiagnosticOperation::Admission);
            EXPECT_EQ(sinkState->records[1].eventKind, NrDiagnosticEventKind::Failure);
            EXPECT_EQ(sinkState->records[1].errorCode, admissionFailure.ErrorCode());
            EXPECT_EQ(sinkState->records[1].nativeErrorCode, admissionFailure.NativeErrorCode());
            EXPECT_EQ(sinkState->records[1].contextFlags, NrDiagnosticContextFlags::HasSessionKey);
            EXPECT_EQ(sinkState->records[1].sessionKey, 82u);
        }

        TEST(NrDiagnosticsQueueTests, CreatesCapacityOneThousandTwentyFourQueueFromDedicatedPoolRole)
        {
            psnr::core::NrResult<std::size_t> storageBytesResult =
                NrDiagnosticsQueue::RequiredStorageBytes(NrDiagnosticsConfigInternal::QueueCapacity);
            ASSERT_TRUE(storageBytesResult.Succeeded());
            EXPECT_EQ(storageBytesResult.Value(), 64u * 1024u);
            EXPECT_EQ(sizeof(psnr::core::NrBoundedMpscQueueSlot<NrDiagnosticRecord>), 64u);

            psnr::core::NrMemoryPoolManagerConfig managerConfig;
            managerConfig.pools = {
                psnr::core::NrMemoryPoolManagerPoolConfig{
                    psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                    psnr::core::NrMemoryPoolConfig{
                        storageBytesResult.Value(),
                        1,
                        psnr::core::NrCacheLineSize,
                    },
                },
            };

            psnr::core::NrResult<std::unique_ptr<psnr::core::NrMemoryPoolManager>> managerResult =
                psnr::core::NrMemoryPoolManager::Create(managerConfig);
            ASSERT_TRUE(managerResult.Succeeded());
            std::unique_ptr<psnr::core::NrMemoryPoolManager> manager = managerResult.TakeValue();

            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsQueue>> queueResult =
                NrDiagnosticsQueue::Create(*manager, psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                                           NrDiagnosticsConfigInternal::QueueCapacity);
            ASSERT_TRUE(queueResult.Succeeded());
            std::unique_ptr<NrDiagnosticsQueue> queue = queueResult.TakeValue();
            EXPECT_EQ(queue->Capacity(), NrDiagnosticsConfigInternal::QueueCapacity);

            psnr::core::NrResult<psnr::core::NrMemoryPoolStats> activeStatsResult =
                manager->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(activeStatsResult.Succeeded());
            EXPECT_EQ(activeStatsResult.Value().capacity, 1u);
            EXPECT_EQ(activeStatsResult.Value().inUse, 1u);

            NrDiagnosticRecord input;
            input.component = NrDiagnosticComponent::Session;
            input.operation = NrDiagnosticOperation::Receive;
            ASSERT_TRUE(queue->TryPush(input).Succeeded());

            NrDiagnosticRecord output;
            ASSERT_TRUE(queue->TryPop(output).Succeeded());
            EXPECT_EQ(output.component, input.component);
            EXPECT_EQ(output.operation, input.operation);

            queue.reset();

            psnr::core::NrResult<psnr::core::NrMemoryPoolStats> releasedStatsResult =
                manager->Stats(psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage);
            ASSERT_TRUE(releasedStatsResult.Succeeded());
            EXPECT_EQ(releasedStatsResult.Value().inUse, 0u);
        }
    } // namespace
} // namespace psnr::runtime::internal
