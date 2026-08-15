#include "pch.h"

#include "NrToWorldHandoff.h"

#include "NrBoundedMpscQueue.h"
#include "NrDiagnosticSink.h"
#include "NrDiagnosticsComponent.h"
#include "NrDiagnosticsConfigInternal.h"
#include "NrMemoryPoolTestUtils.h"
#include "NrServerMetrics.h"
#include "NrServerSubmissionGate.h"
#include "NrSessionActorRegistry.h"
#include "NrSessionActorScheduler.h"
#include "NrSessionSendChannelControl.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace psnr::runtime::internal
{
    namespace
    {
        constexpr std::size_t EventQueueCapacity = 2;
        constexpr std::size_t MaxSessionCount = 3;

        using NrEventQueue = psnr::core::NrBoundedMpscQueue<std::unique_ptr<NrToWorldHandoffEvent>>;
        using NrDiagnosticsQueue = psnr::core::NrBoundedMpscQueue<NrDiagnosticRecord>;

        class NrHandoffCapturingDiagnosticSink final : public INrDiagnosticSink
        {
        public:
            explicit NrHandoffCapturingDiagnosticSink(std::vector<NrDiagnosticRecord>& records) noexcept
                : records_(&records)
            {
            }

            [[nodiscard]] psnr::core::NrStatus Begin(const NrDiagnosticRunMetadata&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Consume(const NrDiagnosticRecord& record) noexcept override
            {
                try
                {
                    records_->push_back(record);
                }
                catch (const std::bad_alloc&)
                {
                    return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::OutOfMemory);
                }
                return psnr::core::NrStatus::Success();
            }

            [[nodiscard]] psnr::core::NrStatus Finish(const NrDiagnosticSummary&) noexcept override
            {
                return psnr::core::NrStatus::Success();
            }

        private:
            std::vector<NrDiagnosticRecord>* records_ = nullptr;
        };

        [[nodiscard]] std::unique_ptr<psnr::core::NrMemoryPoolManager> CreateHandoffMemoryPoolManager()
        {
            const psnr::core::NrResult<std::size_t> storageBytesResult =
                NrEventQueue::RequiredStorageBytes(EventQueueCapacity);
            EXPECT_TRUE(storageBytesResult.Succeeded());
            const psnr::core::NrResult<std::size_t> diagnosticsStorageBytesResult =
                NrDiagnosticsQueue::RequiredStorageBytes(NrDiagnosticsConfigInternal::QueueCapacity);
            EXPECT_TRUE(diagnosticsStorageBytesResult.Succeeded());
            if (storageBytesResult.Failed() || diagnosticsStorageBytesResult.Failed())
            {
                return nullptr;
            }

            psnr::core::NrMemoryPoolManagerConfig config =
                psnr::core::test::MakeMemoryPoolManagerConfigWith(
                    psnr::core::NrMemoryPoolRole::ToWorldEventQueueStorage,
                    psnr::core::test::MakePoolConfig(storageBytesResult.Value(), 1,
                                                     psnr::core::NrCacheLineSize));
            EXPECT_TRUE(psnr::core::test::SetPoolConfig(
                config, psnr::core::NrMemoryPoolRole::DiagnosticsQueueStorage,
                psnr::core::test::MakePoolConfig(diagnosticsStorageBytesResult.Value(), 1,
                                                 psnr::core::NrCacheLineSize)));
            return psnr::core::test::CreateMemoryPoolManager(config);
        }

        [[nodiscard]] std::unique_ptr<NrToWorldHandoff> CreateHandoff(
            psnr::core::NrMemoryPoolManager& memoryPoolManager, NrServerMetrics& metrics,
            NrDiagnosticEmitter diagnosticsEmitter = NrDiagnosticEmitter{})
        {
            psnr::core::NrResult<std::unique_ptr<NrToWorldHandoff>> handoffResult =
                NrToWorldHandoff::Create(memoryPoolManager, metrics, diagnosticsEmitter, MaxSessionCount,
                                         EventQueueCapacity);
            EXPECT_TRUE(handoffResult.Succeeded());
            return handoffResult.Failed() ? nullptr : handoffResult.TakeValue();
        }

        [[nodiscard]] NrServerSubmissionGate CreateSubmissionGate()
        {
            psnr::core::NrResult<NrServerSubmissionGate> gateResult = NrServerSubmissionGate::Create();
            EXPECT_TRUE(gateResult.Succeeded());
            return gateResult.Failed() ? NrServerSubmissionGate{} : gateResult.TakeValue();
        }

        [[nodiscard]] NrSessionSendChannelControlHandle CreateSendChannelControl(
            const psnr::core::NrSessionKey sessionKey, NrSessionActorScheduler& scheduler,
            const NrServerSubmissionGate& submissionGate)
        {
            psnr::core::NrResult<NrSessionSendChannelControlHandle> controlResult =
                NrSessionSendChannelControlHandle::Create(sessionKey, scheduler.ScheduleHandle(),
                                                          submissionGate.CreateAdmissionHandle());
            EXPECT_TRUE(controlResult.Succeeded());
            return controlResult.Failed() ? NrSessionSendChannelControlHandle{} : controlResult.TakeValue();
        }

        [[nodiscard]] std::uint64_t TransactionCount(const NrServerMetrics& metrics,
                                                     const NrPressureTransactionOutcome outcome) noexcept
        {
            return metrics.Capture().pressureTransactionCounts[static_cast<std::size_t>(outcome)];
        }

        TEST(NrToWorldHandoffTests, EmptyWaitTimesOut)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            NrToWorldHandoffWaitResult waitResult = NrToWorldHandoffWaitResult::EventsAvailable;
            ASSERT_TRUE(handoff->WaitForEvents(std::chrono::milliseconds{1}, &waitResult).Succeeded());
            EXPECT_EQ(waitResult, NrToWorldHandoffWaitResult::TimedOut);
        }

        TEST(NrToWorldHandoffTests, FirstQueuedEventWakesWaitingConsumer)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            NrSessionActorRegistry registry(MaxSessionCount);
            NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics);
            NrServerSubmissionGate submissionGate = CreateSubmissionGate();
            ASSERT_TRUE(submissionGate.IsValid());
            NrSessionSendChannelControlHandle control = CreateSendChannelControl(1, scheduler, submissionGate);
            ASSERT_TRUE(control.IsValid());
            ASSERT_TRUE(handoff->ReserveSession(1).Succeeded());

            std::atomic<bool> waitStarted = false;
            NrToWorldHandoffWaitResult waitResult = NrToWorldHandoffWaitResult::TimedOut;
            psnr::core::NrStatus waitStatus =
                psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
            std::thread consumer(
                [&]() noexcept
                {
                    waitStarted.store(true, std::memory_order_release);
                    waitStatus = handoff->WaitForEvents(std::chrono::seconds{1}, &waitResult);
                });

            while (!waitStarted.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            const psnr::core::NrStatus recordStatus = handoff->RecordAccepted(1, *control.Get());
            consumer.join();

            EXPECT_TRUE(recordStatus.Succeeded());
            EXPECT_TRUE(waitStatus.Succeeded());
            EXPECT_EQ(waitResult, NrToWorldHandoffWaitResult::EventsAvailable);
        }

        TEST(NrToWorldHandoffTests, CloseWakesWaitingConsumer)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            std::atomic<bool> waitStarted = false;
            NrToWorldHandoffWaitResult closedWaitResult = NrToWorldHandoffWaitResult::TimedOut;
            psnr::core::NrStatus waitStatus =
                psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
            std::thread consumer(
                [&]() noexcept
                {
                    waitStarted.store(true, std::memory_order_release);
                    waitStatus = handoff->WaitForEvents(std::chrono::seconds{1}, &closedWaitResult);
                });

            while (!waitStarted.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            EXPECT_TRUE(handoff->Close().Succeeded());
            consumer.join();
            EXPECT_TRUE(waitStatus.Succeeded());
            EXPECT_EQ(closedWaitResult, NrToWorldHandoffWaitResult::Closed);
            EXPECT_TRUE(handoff->Close().Succeeded());
        }

        TEST(NrToWorldHandoffTests, CloseReportsAvailableUntilQueuedEventsAreDrained)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            NrSessionActorRegistry registry(MaxSessionCount);
            NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics);
            NrServerSubmissionGate submissionGate = CreateSubmissionGate();
            ASSERT_TRUE(submissionGate.IsValid());
            NrSessionSendChannelControlHandle control = CreateSendChannelControl(1, scheduler, submissionGate);
            ASSERT_TRUE(control.IsValid());
            ASSERT_TRUE(handoff->ReserveSession(1).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(1, *control.Get()).Succeeded());
            ASSERT_TRUE(handoff->Close().Succeeded());

            NrToWorldHandoffWaitResult waitResult = NrToWorldHandoffWaitResult::TimedOut;
            ASSERT_TRUE(handoff->WaitForEvents(std::chrono::nanoseconds::zero(), &waitResult).Succeeded());
            EXPECT_EQ(waitResult, NrToWorldHandoffWaitResult::EventsAvailable);

            std::unique_ptr<NrToWorldHandoffEvent> event;
            ASSERT_TRUE(handoff->TryPop(event).Succeeded());
            ASSERT_NE(event, nullptr);

            ASSERT_TRUE(handoff->WaitForEvents(std::chrono::nanoseconds::zero(), &waitResult).Succeeded());
            EXPECT_EQ(waitResult, NrToWorldHandoffWaitResult::Closed);
        }

        TEST(NrToWorldHandoffTests, BatchPopPreservesOrderAndPublishesPendingLifecycleWithinOneCall)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            NrSessionActorRegistry registry(MaxSessionCount);
            NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics);
            NrServerSubmissionGate submissionGate = CreateSubmissionGate();
            ASSERT_TRUE(submissionGate.IsValid());
            NrSessionSendChannelControlHandle firstControl =
                CreateSendChannelControl(1, scheduler, submissionGate);
            NrSessionSendChannelControlHandle secondControl =
                CreateSendChannelControl(2, scheduler, submissionGate);
            NrSessionSendChannelControlHandle thirdControl =
                CreateSendChannelControl(3, scheduler, submissionGate);
            ASSERT_TRUE(firstControl.IsValid());
            ASSERT_TRUE(secondControl.IsValid());
            ASSERT_TRUE(thirdControl.IsValid());

            ASSERT_TRUE(handoff->ReserveSession(1).Succeeded());
            ASSERT_TRUE(handoff->ReserveSession(2).Succeeded());
            ASSERT_TRUE(handoff->ReserveSession(3).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(1, *firstControl.Get()).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(2, *secondControl.Get()).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(3, *thirdControl.Get()).Succeeded());
            ASSERT_EQ(handoff->PendingSlotCount(), 1u);

            std::array<std::unique_ptr<NrToWorldHandoffEvent>, EventQueueCapacity> eventBuffer;
            std::size_t eventCount = 99;
            ASSERT_TRUE(handoff->TryPopBatch(eventBuffer, &eventCount).Succeeded());

            ASSERT_EQ(eventCount, EventQueueCapacity);
            ASSERT_NE(eventBuffer[0], nullptr);
            ASSERT_NE(eventBuffer[1], nullptr);
            EXPECT_EQ(eventBuffer[0]->Kind(), NrToWorldHandoffEventKind::SessionAccepted);
            EXPECT_EQ(eventBuffer[0]->SessionKey(), 1u);
            EXPECT_EQ(eventBuffer[1]->Kind(), NrToWorldHandoffEventKind::SessionAccepted);
            EXPECT_EQ(eventBuffer[1]->SessionKey(), 2u);
            EXPECT_EQ(handoff->PendingSlotCount(), 0u);

            std::unique_ptr<NrToWorldHandoffEvent> promotedEvent;
            ASSERT_TRUE(handoff->TryPop(promotedEvent).Succeeded());
            ASSERT_NE(promotedEvent, nullptr);
            EXPECT_EQ(promotedEvent->Kind(), NrToWorldHandoffEventKind::SessionAccepted);
            EXPECT_EQ(promotedEvent->SessionKey(), 3u);
        }

        TEST(NrToWorldHandoffTests, BatchPopRejectsInvalidArgumentsAndPreservesOutputCount)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            std::array<std::unique_ptr<NrToWorldHandoffEvent>, 1> eventBuffer;
            std::size_t eventCount = 99;

            EXPECT_EQ(handoff
                          ->TryPopBatch(std::span<std::unique_ptr<NrToWorldHandoffEvent>>{}, &eventCount)
                          .ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(handoff->TryPopBatch(eventBuffer, nullptr).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidArgument);
            EXPECT_EQ(eventCount, 99u);
            EXPECT_EQ(eventBuffer[0], nullptr);

            eventBuffer[0] = NrToWorldHandoffEvent::CreateSessionClosed(
                                 1, NrSessionEndReason::ApplicationRequested)
                                 .TakeValue();
            EXPECT_EQ(handoff->TryPopBatch(eventBuffer, &eventCount).ErrorCode(),
                      psnr::core::NrErrorCode::InvalidState);
            EXPECT_EQ(eventCount, 99u);
            EXPECT_NE(eventBuffer[0], nullptr);
        }

        TEST(NrToWorldHandoffTests, PacketQueueFullRecordsEachRejectedAdmissionTransaction)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            NrToWorldHandoffStats handoffStats = handoff->Stats();
            EXPECT_EQ(handoffStats.eventDepth, 0u);
            EXPECT_EQ(handoffStats.eventHighWatermark, 0u);

            NrSessionActorRegistry registry(MaxSessionCount);
            NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics);
            NrServerSubmissionGate submissionGate = CreateSubmissionGate();
            ASSERT_TRUE(submissionGate.IsValid());
            NrSessionSendChannelControlHandle firstControl =
                CreateSendChannelControl(1, scheduler, submissionGate);
            NrSessionSendChannelControlHandle secondControl =
                CreateSendChannelControl(2, scheduler, submissionGate);
            ASSERT_TRUE(firstControl.IsValid());
            ASSERT_TRUE(secondControl.IsValid());

            ASSERT_TRUE(handoff->ReserveSession(1).Succeeded());
            ASSERT_TRUE(handoff->ReserveSession(2).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(1, *firstControl.Get()).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(2, *secondControl.Get()).Succeeded());

            handoffStats = handoff->Stats();
            EXPECT_EQ(handoffStats.eventDepth, EventQueueCapacity);
            EXPECT_EQ(handoffStats.eventHighWatermark, EventQueueCapacity);

            const psnr::core::NrStatus firstReject = handoff->RecordPacket(1, psnr::core::NrPacketType{1}, {});
            const psnr::core::NrStatus secondReject = handoff->RecordPacket(1, psnr::core::NrPacketType{1}, {});

            EXPECT_EQ(firstReject.ErrorCode(), psnr::core::NrErrorCode::QueueFull);
            EXPECT_EQ(secondReject.ErrorCode(), psnr::core::NrErrorCode::QueueFull);
            EXPECT_EQ(TransactionCount(metrics, NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected), 2u);

            std::unique_ptr<NrToWorldHandoffEvent> event;
            ASSERT_TRUE(handoff->TryPop(event).Succeeded());
            ASSERT_NE(event, nullptr);
            EXPECT_EQ(event->Kind(), NrToWorldHandoffEventKind::SessionAccepted);
            EXPECT_EQ(event->SessionKey(), 1u);

            handoffStats = handoff->Stats();
            EXPECT_EQ(handoffStats.eventDepth, EventQueueCapacity - 1);
            EXPECT_EQ(handoffStats.eventHighWatermark, EventQueueCapacity);
        }

        TEST(NrToWorldHandoffTests, LifecyclePublicationDeferredRecordsOncePerPendingLifecycleFact)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);
            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff = CreateHandoff(*memoryPoolManager, metrics);
            ASSERT_NE(handoff, nullptr);

            NrSessionActorRegistry registry(MaxSessionCount);
            NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics);
            NrServerSubmissionGate submissionGate = CreateSubmissionGate();
            ASSERT_TRUE(submissionGate.IsValid());
            NrSessionSendChannelControlHandle firstControl =
                CreateSendChannelControl(1, scheduler, submissionGate);
            NrSessionSendChannelControlHandle secondControl =
                CreateSendChannelControl(2, scheduler, submissionGate);
            NrSessionSendChannelControlHandle thirdControl =
                CreateSendChannelControl(3, scheduler, submissionGate);
            ASSERT_TRUE(firstControl.IsValid());
            ASSERT_TRUE(secondControl.IsValid());
            ASSERT_TRUE(thirdControl.IsValid());

            ASSERT_TRUE(handoff->ReserveSession(1).Succeeded());
            ASSERT_TRUE(handoff->ReserveSession(2).Succeeded());
            ASSERT_TRUE(handoff->ReserveSession(3).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(1, *firstControl.Get()).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(2, *secondControl.Get()).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(3, *thirdControl.Get()).Succeeded());

            EXPECT_EQ(handoff->PendingSlotCount(), 1u);
            EXPECT_EQ(TransactionCount(metrics,
                                       NrPressureTransactionOutcome::ToWorldLifecyclePublicationDeferred),
                      1u);

            const psnr::core::NrStatus pendingAcceptedPacketReject =
                handoff->RecordPacket(3, psnr::core::NrPacketType{1}, {});
            EXPECT_EQ(pendingAcceptedPacketReject.ErrorCode(), psnr::core::NrErrorCode::QueueFull);
            EXPECT_EQ(TransactionCount(metrics, NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected), 1u);

            std::unique_ptr<NrToWorldHandoffEvent> event;
            ASSERT_TRUE(handoff->TryPop(event).Succeeded());
            ASSERT_NE(event, nullptr);
            EXPECT_EQ(TransactionCount(metrics,
                                       NrPressureTransactionOutcome::ToWorldLifecyclePublicationDeferred),
                      1u);
        }

        TEST(NrToWorldHandoffDiagnosticsTests, EmitsAcceptedAndClosedTransitionOnceWithFinalReason)
        {
            std::unique_ptr<psnr::core::NrMemoryPoolManager> memoryPoolManager =
                CreateHandoffMemoryPoolManager();
            ASSERT_NE(memoryPoolManager, nullptr);

            std::vector<NrDiagnosticRecord> records;
            const NrDiagnosticsConfigInternal diagnosticsConfig{NrDiagnosticsMode::Debug, {}};
            psnr::core::NrResult<std::unique_ptr<NrDiagnosticsComponent>> diagnosticsResult =
                NrDiagnosticsComponent::Create(
                    diagnosticsConfig, *memoryPoolManager,
                    std::make_unique<NrHandoffCapturingDiagnosticSink>(records));
            ASSERT_TRUE(diagnosticsResult.Succeeded());
            std::unique_ptr<NrDiagnosticsComponent> diagnostics = diagnosticsResult.TakeValue();
            NrBootstrapContext bootstrapContext;
            ASSERT_TRUE(diagnostics->Configure(bootstrapContext).Succeeded());
            ASSERT_TRUE(diagnostics->Start().Succeeded());

            NrServerMetrics metrics;
            std::unique_ptr<NrToWorldHandoff> handoff =
                CreateHandoff(*memoryPoolManager, metrics, diagnostics->Emitter());
            ASSERT_NE(handoff, nullptr);

            NrSessionActorRegistry registry(MaxSessionCount);
            NrSessionActorScheduler scheduler(registry, *memoryPoolManager, metrics);
            NrServerSubmissionGate submissionGate = CreateSubmissionGate();
            ASSERT_TRUE(submissionGate.IsValid());
            NrSessionSendChannelControlHandle control =
                CreateSendChannelControl(1, scheduler, submissionGate);
            ASSERT_TRUE(control.IsValid());

            ASSERT_TRUE(handoff->ReserveSession(1).Succeeded());
            ASSERT_TRUE(handoff->RecordAccepted(1, *control.Get()).Succeeded());
            ASSERT_TRUE(handoff->RecordClosed(1, NrSessionEndReason::RemoteClosed).Succeeded());
            EXPECT_TRUE(handoff->RecordClosed(1, NrSessionEndReason::TransportError).Failed());

            ASSERT_TRUE(diagnostics->RequestStop(NrStopContext{}).Succeeded());
            ASSERT_TRUE(diagnostics->Shutdown().Succeeded());

            ASSERT_EQ(records.size(), 2u);
            EXPECT_EQ(records[0].severity, NrDiagnosticSeverity::Info);
            EXPECT_EQ(records[0].eventKind, NrDiagnosticEventKind::Transition);
            EXPECT_EQ(records[0].component, NrDiagnosticComponent::Session);
            EXPECT_EQ(records[0].operation, NrDiagnosticOperation::Accept);
            EXPECT_EQ(records[0].contextFlags, NrDiagnosticContextFlags::HasSessionKey);
            EXPECT_EQ(records[0].sessionKey, 1u);

            EXPECT_EQ(records[1].severity, NrDiagnosticSeverity::Info);
            EXPECT_EQ(records[1].eventKind, NrDiagnosticEventKind::Transition);
            EXPECT_EQ(records[1].component, NrDiagnosticComponent::Session);
            EXPECT_EQ(records[1].operation, NrDiagnosticOperation::Close);
            EXPECT_EQ(records[1].contextFlags,
                      static_cast<NrDiagnosticContextFlags>(
                          static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey) |
                          static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasCloseReason)));
            EXPECT_EQ(records[1].sessionKey, 1u);
            EXPECT_EQ(records[1].closeReason, NrSessionEndReason::RemoteClosed);
        }
    } // namespace
} // namespace psnr::runtime::internal
