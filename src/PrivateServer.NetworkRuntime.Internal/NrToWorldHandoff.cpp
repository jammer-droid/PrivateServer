#include "pch.h"

#include "NrToWorldHandoff.h"

#include "NrBoundedMpscQueue.h"
#include "NrConcurrency.h"
#include "NrErrorCode.h"
#include "NrMemoryPoolManager.h"
#include "NrServerMetrics.h"
#include "NrToWorldLifecycleState.h"

#include <cassert>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrBoundedMpscQueue;
    using psnr::core::NrErrorCode;
    using psnr::core::NrMemoryPoolManager;
    using psnr::core::NrMemoryPoolRole;
    using psnr::core::NrPacketType;
    using psnr::core::NrResult;
    using psnr::core::NrScopedLock;
    using psnr::core::NrSessionKey;
    using psnr::core::NrStatus;
    using psnr::core::NrWaitLock;

    NrResult<std::unique_ptr<NrToWorldHandoffEvent>> NrToWorldHandoffEvent::CreateSessionAccepted(
        const NrSessionKey sessionKey, NrSessionSendChannelControl& sendChannelControl) noexcept
    {
        if (sessionKey == 0)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::InvalidArgument);
        }

        std::unique_ptr<NrToWorldHandoffEvent> event(
            new (std::nothrow) NrToWorldHandoffEvent(NrToWorldHandoffEventKind::SessionAccepted, sessionKey));
        if (event == nullptr)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::OutOfMemory);
        }

        event->sendChannelControl_ = NrSessionSendChannelControlHandle::Retain(sendChannelControl);
        return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>(std::move(event));
    }

    NrResult<std::unique_ptr<NrToWorldHandoffEvent>> NrToWorldHandoffEvent::CreatePacketReceived(
        const NrSessionKey sessionKey, const psnr::core::NrPacketType packetType,
        const std::span<const std::byte> payload) noexcept
    {
        if (sessionKey == 0 || payload.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::InvalidArgument);
        }

        std::unique_ptr<NrToWorldHandoffEvent> event(
            new (std::nothrow) NrToWorldHandoffEvent(NrToWorldHandoffEventKind::PacketReceived, sessionKey));
        if (event == nullptr)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::OutOfMemory);
        }

        if (!payload.empty())
        {
            event->payload_.reset(new (std::nothrow) std::byte[payload.size()]);
            if (event->payload_ == nullptr)
            {
                return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::OutOfMemory);
            }
            std::memcpy(event->payload_.get(), payload.data(), payload.size());
        }

        event->packetType_ = packetType;
        event->payloadLength_ = static_cast<std::uint32_t>(payload.size());
        return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>(std::move(event));
    }

    NrResult<std::unique_ptr<NrToWorldHandoffEvent>> NrToWorldHandoffEvent::CreateSessionClosed(
        const NrSessionKey sessionKey, const NrSessionEndReason endReason) noexcept
    {
        if (sessionKey == 0 || endReason == NrSessionEndReason::None)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::InvalidArgument);
        }

        std::unique_ptr<NrToWorldHandoffEvent> event(
            new (std::nothrow) NrToWorldHandoffEvent(NrToWorldHandoffEventKind::SessionClosed, sessionKey));
        if (event == nullptr)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::OutOfMemory);
        }

        event->endReason_ = endReason;
        return NrResult<std::unique_ptr<NrToWorldHandoffEvent>>(std::move(event));
    }

    NrToWorldHandoffEventKind NrToWorldHandoffEvent::Kind() const noexcept
    {
        return kind_;
    }

    NrSessionKey NrToWorldHandoffEvent::SessionKey() const noexcept
    {
        return sessionKey_;
    }

    NrSessionSendChannelControl* NrToWorldHandoffEvent::SendChannelControl() const noexcept
    {
        return sendChannelControl_.Get();
    }

    psnr::core::NrPacketType NrToWorldHandoffEvent::PacketType() const noexcept
    {
        return packetType_;
    }

    std::span<const std::byte> NrToWorldHandoffEvent::Payload() const noexcept
    {
        return std::span<const std::byte>(payload_.get(), payloadLength_);
    }

    NrSessionEndReason NrToWorldHandoffEvent::EndReason() const noexcept
    {
        return endReason_;
    }

    NrToWorldHandoffEvent::NrToWorldHandoffEvent(const NrToWorldHandoffEventKind kind,
                                                 const NrSessionKey sessionKey) noexcept
        : kind_(kind)
        , sessionKey_(sessionKey)
    {
    }

    struct NrToWorldHandoff::Impl final
    {
        struct NrPublicationSlot final
        {
            NrToWorldLifecycleState lifecycleState;
            NrSessionSendChannelControlHandle sendChannelControl;
            bool pendingRegistered = false;
            NrToWorldLifecycleNotificationKind lastDeferredKind = NrToWorldLifecycleNotificationKind::None;
        };

        struct NrPendingEntry final
        {
            NrSessionKey sessionKey = 0;
        };

        using NrEventQueue = NrBoundedMpscQueue<std::unique_ptr<NrToWorldHandoffEvent>>;
        using NrPublicationSlotMap = std::unordered_map<NrSessionKey, NrPublicationSlot>;

        Impl(std::unique_ptr<NrEventQueue> eventQueueValue, NrServerMetrics& metricsValue,
             NrDiagnosticEmitter diagnosticsEmitterValue, std::size_t maxSessionCountValue,
             std::unique_ptr<NrPendingEntry[]> pendingEntriesValue) noexcept
            : eventQueue(std::move(eventQueueValue))
            , metrics(&metricsValue)
            , diagnosticsEmitter(diagnosticsEmitterValue)
            , maxSessionCount(maxSessionCountValue)
            , pendingEntries(std::move(pendingEntriesValue))
        {
        }

        [[nodiscard]] NrStatus InitializeSlotMap() noexcept
        {
            try
            {
                publicationSlots.reserve(maxSessionCount);
            }
            catch (const std::bad_alloc&)
            {
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }
            catch (const std::length_error&)
            {
                return NrStatus::Failure(NrErrorCode::CapacityExceeded);
            }
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus ReserveSession(NrSessionKey sessionKey) noexcept
        {
            if (sessionKey == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            NrScopedLock guard(lock);
            if (closed)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }
            if (publicationSlots.find(sessionKey) != publicationSlots.end())
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }
            if (publicationSlots.size() >= maxSessionCount)
            {
                return NrStatus::Failure(NrErrorCode::CapacityExceeded);
            }

            try
            {
                const std::pair<NrPublicationSlotMap::iterator, bool> insertResult =
                    publicationSlots.try_emplace(sessionKey);
                if (!insertResult.second)
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }
            }
            catch (const std::bad_alloc&)
            {
                return NrStatus::Failure(NrErrorCode::OutOfMemory);
            }
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus CancelSessionReservation(NrSessionKey sessionKey) noexcept
        {
            NrScopedLock guard(lock);
            if (closed)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }
            const NrPublicationSlotMap::iterator iterator = publicationSlots.find(sessionKey);
            if (iterator == publicationSlots.end())
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            const NrPublicationSlot& slot = iterator->second;
            if (slot.sendChannelControl.IsValid() || slot.pendingRegistered ||
                slot.lifecycleState.NextPendingKind() != NrToWorldLifecycleNotificationKind::None)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            publicationSlots.erase(iterator);
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus RecordAccepted(NrSessionKey sessionKey,
                                              NrSessionSendChannelControl& sendChannelControl) noexcept
        {
            if (sessionKey == 0)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            NrScopedLock guard(lock);
            if (closed)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }
            const NrPublicationSlotMap::iterator iterator = publicationSlots.find(sessionKey);
            if (iterator == publicationSlots.end())
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            NrPublicationSlot& slot = iterator->second;
            const NrStatus recordStatus = slot.lifecycleState.RecordAccepted();
            if (recordStatus.Failed())
            {
                return recordStatus;
            }
            slot.sendChannelControl = NrSessionSendChannelControlHandle::Retain(sendChannelControl);
            EmitSessionAccepted(sessionKey);

            const NrStatus promoteStatus = PromoteOne(sessionKey, slot);
            if (promoteStatus.Failed())
            {
                // Accepted fact는 이미 slot에 기록됐다. 즉시 event 생성/push가 실패해도 pending에서
                // 재시도할 수 있으므로 producer에는 publication 보존 성공으로 보고한다.
                assert(promoteStatus.ErrorCode() != NrErrorCode::InvalidArgument &&
                       promoteStatus.ErrorCode() != NrErrorCode::InvalidState);
                RecordLifecyclePublicationDeferred(slot);
                RegisterPending(sessionKey, slot);
                return NrStatus::Success();
            }

            assert(slot.lifecycleState.NextPendingKind() == NrToWorldLifecycleNotificationKind::None);
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus RecordClosed(NrSessionKey sessionKey, NrSessionEndReason endReason) noexcept
        {
            NrScopedLock guard(lock);
            if (closed)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            const NrPublicationSlotMap::iterator iterator = publicationSlots.find(sessionKey);
            if (iterator == publicationSlots.end())
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            NrPublicationSlot& slot = iterator->second;
            if (endReason == NrSessionEndReason::ServerStopping && !slot.sendChannelControl.IsValid() &&
                slot.lifecycleState.NextPendingKind() == NrToWorldLifecycleNotificationKind::None)
            {
                // Accepted fact가 기록되기 전 shutdown된 reservation-only session은 World lifecycle에 노출하지 않는다.
                publicationSlots.erase(iterator);
                return NrStatus::Success();
            }

            const NrStatus recordStatus = slot.lifecycleState.RecordClosed(endReason);
            if (recordStatus.Failed())
            {
                return recordStatus;
            }
            EmitSessionClosed(sessionKey, endReason);
            if (slot.pendingRegistered)
            {
                return NrStatus::Success();
            }

            const NrStatus promoteStatus = PromoteOne(sessionKey, slot);
            if (promoteStatus.Failed())
            {
                // Closed fact는 이미 slot에 기록됐다. 즉시 event 생성/push가 실패해도 pending에서
                // 재시도할 수 있으므로 producer에는 publication 보존 성공으로 보고한다.
                assert(promoteStatus.ErrorCode() != NrErrorCode::InvalidArgument &&
                       promoteStatus.ErrorCode() != NrErrorCode::InvalidState);
                RecordLifecyclePublicationDeferred(slot);
                RegisterPending(sessionKey, slot);
                return NrStatus::Success();
            }

            assert(slot.lifecycleState.NextPendingKind() == NrToWorldLifecycleNotificationKind::None);
            assert(slot.lifecycleState.IsSessionClosedPublished());
            publicationSlots.erase(iterator);
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus RecordPacket(NrSessionKey sessionKey, NrPacketType packetType,
                                            std::span<const std::byte> payload) noexcept
        {
            NrScopedLock guard(lock);
            if (closed)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            const NrPublicationSlotMap::iterator iterator = publicationSlots.find(sessionKey);
            if (iterator == publicationSlots.end())
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            NrPublicationSlot& slot = iterator->second;
            if (!slot.lifecycleState.CanPublishPacket())
            {
                // Accepted가 아직 queue에 commit되지 않았다면 packet을 따로 보존하지 않고 pressure로 보고한다.
                if (slot.lifecycleState.NextPendingKind() == NrToWorldLifecycleNotificationKind::SessionAccepted)
                {
                    metrics->Record(NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);
                    return NrStatus::Failure(NrErrorCode::QueueFull);
                }

                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            NrResult<std::unique_ptr<NrToWorldHandoffEvent>> eventResult =
                NrToWorldHandoffEvent::CreatePacketReceived(sessionKey, packetType, payload);
            if (eventResult.Failed())
            {
                return eventResult.Status();
            }

            // Packet은 lifecycle처럼 pending slot에 보존하지 않는다. QueueFull은 producer의 pressure 정책 입력이다.(hard pressure)
            const NrStatus pushStatus = TryPushEvent(eventResult.TakeValue());
            if (pushStatus.ErrorCode() == NrErrorCode::QueueFull)
            {
                metrics->Record(NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected);
            }
            return pushStatus;
        }

        [[nodiscard]] NrStatus TryPop(std::unique_ptr<NrToWorldHandoffEvent>& outEvent) noexcept
        {
            if (outEvent != nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            NrScopedLock guard(lock);
            return TryPopOneLocked(outEvent);
        }

        [[nodiscard]] NrStatus TryPopBatch(const std::span<std::unique_ptr<NrToWorldHandoffEvent>> eventBuffer,
                                           std::size_t* const outEventCount) noexcept
        {
            if (eventBuffer.empty() || outEventCount == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }
            for (const std::unique_ptr<NrToWorldHandoffEvent>& event : eventBuffer)
            {
                if (event != nullptr)
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }
            }

            NrScopedLock guard(lock);

            std::size_t eventCount = 0;
            while (eventCount < eventBuffer.size())
            {
                const NrStatus popStatus = TryPopOneLocked(eventBuffer[eventCount]);
                if (popStatus.Failed())
                {
                    if (popStatus.ErrorCode() == NrErrorCode::QueueEmpty && eventCount != 0)
                    {
                        break;
                    }
                    return popStatus;
                }

                ++eventCount;
            }

            *outEventCount = eventCount;
            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus WaitForEvents(const std::chrono::nanoseconds timeout,
                                             NrToWorldHandoffWaitResult* const outWaitResult) noexcept
        {
            if (timeout < std::chrono::nanoseconds::zero() || outWaitResult == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            try
            {
                NrWaitLock guard(lock);
                const bool predicateSatisfied =
                    eventAvailable.wait_for(guard, timeout, [this]() noexcept { return eventDepth != 0 || closed; });
                if (!predicateSatisfied) // event 존재 or closed 상태가 아님
                {
                    *outWaitResult = NrToWorldHandoffWaitResult::TimedOut;
                }
                else if (eventDepth != 0) // event가 있는 상태
                {
                    // Close 이후에도 queue가 source of truth다. 남은 event를 모두 drain한 뒤 Closed를 관찰한다.
                    *outWaitResult = NrToWorldHandoffWaitResult::EventsAvailable;
                }
                else // closed 상태
                {
                    *outWaitResult = NrToWorldHandoffWaitResult::Closed;
                }
            }
            catch (const std::system_error&)
            {
                return NrStatus::Failure(NrErrorCode::IoFailed);
            }

            return NrStatus::Success();
        }

        [[nodiscard]] NrStatus Close() noexcept
        {
            {
                NrScopedLock guard(lock);
                if (closed)
                {
                    return NrStatus::Success();
                }
                closed = true;
            }
            eventAvailable.notify_all();
            return NrStatus::Success();
        }

        // lock을 획득한 호출자만 사용한다.
        [[nodiscard]] NrStatus TryPopOneLocked(std::unique_ptr<NrToWorldHandoffEvent>& outEvent) noexcept
        {
            assert(outEvent == nullptr);

            PromotePending();

            const NrStatus popStatus = eventQueue->TryPop(outEvent);
            if (popStatus.Failed())
            {
                return popStatus;
            }

            assert(eventDepth != 0);
            --eventDepth;

            PromotePending();
            return NrStatus::Success();
        }

        // slot -> event queue push
        [[nodiscard]] NrStatus PromoteOne(NrSessionKey sessionKey, NrPublicationSlot& slot) noexcept
        {
            const NrToWorldLifecycleNotificationKind kind = slot.lifecycleState.NextPendingKind();
            NrResult<std::unique_ptr<NrToWorldHandoffEvent>> eventResult =
                NrResult<std::unique_ptr<NrToWorldHandoffEvent>>::Failure(NrErrorCode::InvalidState);

            switch (kind)
            {
            case NrToWorldLifecycleNotificationKind::SessionAccepted:
                if (!slot.sendChannelControl.IsValid())
                {
                    return NrStatus::Failure(NrErrorCode::InvalidState);
                }
                eventResult = NrToWorldHandoffEvent::CreateSessionAccepted(sessionKey, *slot.sendChannelControl.Get());
                break;

            case NrToWorldLifecycleNotificationKind::SessionClosed:
            {
                NrSessionEndReason endReason = NrSessionEndReason::None;
                const NrStatus endReasonStatus = slot.lifecycleState.GetEndReason(endReason);
                if (endReasonStatus.Failed())
                {
                    return endReasonStatus;
                }
                eventResult = NrToWorldHandoffEvent::CreateSessionClosed(sessionKey, endReason);
                break;
            }

            case NrToWorldLifecycleNotificationKind::None:
                return NrStatus::Failure(NrErrorCode::InvalidState);
            }

            if (eventResult.Failed())
            {
                return eventResult.Status();
            }

            const NrStatus pushStatus = TryPushEvent(eventResult.TakeValue());
            return pushStatus.Failed() ? pushStatus : slot.lifecycleState.CommitNextPending();
        }

        [[nodiscard]] NrStatus TryPushEvent(std::unique_ptr<NrToWorldHandoffEvent>&& event) noexcept
        {
            const bool wasEmpty = (eventDepth == 0);
            const NrStatus pushStatus = eventQueue->TryPush(std::move(event));
            if (pushStatus.Succeeded())
            {
                assert(eventDepth < eventQueue->Capacity());
                ++eventDepth;
                if (eventHighWatermark < eventDepth)
                {
                    eventHighWatermark = eventDepth;
                }
                if (wasEmpty) // queue 가 비어있다가 push 된 상태면 notify로 wake-up
                {
                    eventAvailable.notify_one();
                }
            }
            return pushStatus;
        }

        void PromotePending() noexcept
        {
            const std::size_t turnCount = pendingSize;
            for (std::size_t turn = 0; turn < turnCount && pendingSize != 0; ++turn)
            {
                const NrPendingEntry entry = pendingEntries[pendingHead];
                pendingHead = (pendingHead + 1) % maxSessionCount;
                --pendingSize;

                const NrPublicationSlotMap::iterator iterator = publicationSlots.find(entry.sessionKey);
                // Runtime Session Key는 프로세스 수명 동안 재사용되지 않으므로 slot 세대 비교가 필요 없다.
                if (iterator == publicationSlots.end())
                {
                    continue;
                }

                NrPublicationSlot& slot = iterator->second;
                slot.pendingRegistered = false;
                const NrStatus promoteStatus = PromoteOne(entry.sessionKey, slot);
                if (promoteStatus.Failed())
                {
                    RecordLifecyclePublicationDeferred(slot);
                    RegisterPending(entry.sessionKey, slot);
                    if (promoteStatus.ErrorCode() == NrErrorCode::QueueFull)
                    {
                        break;
                    }
                    continue;
                }

                if (slot.lifecycleState.NextPendingKind() != NrToWorldLifecycleNotificationKind::None)
                {
                    RegisterPending(entry.sessionKey, slot);
                }
                else if (slot.lifecycleState.IsSessionClosedPublished())
                {
                    publicationSlots.erase(iterator);
                }
            }
        }

        void RegisterPending(NrSessionKey sessionKey, NrPublicationSlot& slot) noexcept
        {
            if (slot.pendingRegistered) // already registered
            {
                return;
            }

            assert(pendingSize < maxSessionCount);

            if (pendingSize >= maxSessionCount)
            {
                return;
            }

            const std::size_t tail = (pendingHead + pendingSize) % maxSessionCount;
            pendingEntries[tail] = NrPendingEntry{sessionKey};
            ++pendingSize;
            slot.pendingRegistered = true;
        }

        void RecordLifecyclePublicationDeferred(NrPublicationSlot& slot) noexcept
        {
            const NrToWorldLifecycleNotificationKind kind = slot.lifecycleState.NextPendingKind();

            assert(kind != NrToWorldLifecycleNotificationKind::None);

            // lastDeferredKind 가 동일한 경우에는 Record 안함
            // ex) 이전 - SessionAccepted, 현재 - SessionAccepted인 경우 같은 상태에서 retry로 다시 호출된 경우라 집계 X
            if (slot.lastDeferredKind == kind)
            {
                return;
            }

            slot.lastDeferredKind = kind;
            metrics->Record(NrPressureTransactionOutcome::ToWorldLifecyclePublicationDeferred);
        }

        void EmitSessionAccepted(const NrSessionKey sessionKey) const noexcept
        {
            NrDiagnosticRecord record;
            record.sessionKey = sessionKey;
            record.component = NrDiagnosticComponent::Session;
            record.operation = NrDiagnosticOperation::Accept;
            record.severity = NrDiagnosticSeverity::Info;
            record.eventKind = NrDiagnosticEventKind::Transition;
            record.contextFlags = NrDiagnosticContextFlags::HasSessionKey;
            diagnosticsEmitter.Emit(record);
        }

        void EmitSessionClosed(const NrSessionKey sessionKey, const NrSessionEndReason endReason) const noexcept
        {
            NrDiagnosticRecord record;
            record.sessionKey = sessionKey;
            record.component = NrDiagnosticComponent::Session;
            record.operation = NrDiagnosticOperation::Close;
            record.severity = NrDiagnosticSeverity::Info;
            record.eventKind = NrDiagnosticEventKind::Transition;
            record.contextFlags = static_cast<NrDiagnosticContextFlags>(
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasSessionKey) |
                static_cast<std::uint8_t>(NrDiagnosticContextFlags::HasCloseReason));
            record.closeReason = endReason;
            diagnosticsEmitter.Emit(record);
        }

        std::unique_ptr<NrEventQueue> eventQueue;
        NrServerMetrics* metrics = nullptr;
        NrDiagnosticEmitter diagnosticsEmitter;
        NrPublicationSlotMap publicationSlots;
        std::size_t maxSessionCount = 0;

        std::unique_ptr<NrPendingEntry[]> pendingEntries;
        std::size_t pendingHead = 0;
        std::size_t pendingSize = 0;
        std::size_t eventDepth = 0;
        std::size_t eventHighWatermark = 0;
        mutable psnr::core::NrMutex lock;
        std::condition_variable_any eventAvailable;
        bool closed = false;
    };

    NrToWorldHandoff::~NrToWorldHandoff() noexcept = default;

    NrResult<std::unique_ptr<NrToWorldHandoff>> NrToWorldHandoff::Create(NrMemoryPoolManager& memoryPoolManager,
                                                                         NrServerMetrics& metrics,
                                                                         NrDiagnosticEmitter diagnosticsEmitter,
                                                                         const std::size_t maxSessionCount,
                                                                         const std::size_t eventQueueCapacity) noexcept
    {
        if (maxSessionCount == 0 ||
            maxSessionCount > std::numeric_limits<std::size_t>::max() / sizeof(Impl::NrPendingEntry))
        {
            return NrResult<std::unique_ptr<NrToWorldHandoff>>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<std::unique_ptr<Impl::NrEventQueue>> queueResult = Impl::NrEventQueue::Create(
            memoryPoolManager, NrMemoryPoolRole::ToWorldEventQueueStorage, eventQueueCapacity);
        if (queueResult.Failed())
        {
            return NrResult<std::unique_ptr<NrToWorldHandoff>>::Failure(queueResult.Status());
        }

        std::unique_ptr<Impl::NrPendingEntry[]> pendingEntries(new (std::nothrow)
                                                                   Impl::NrPendingEntry[maxSessionCount]);
        if (pendingEntries == nullptr)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoff>>::Failure(NrErrorCode::OutOfMemory);
        }

        std::unique_ptr<Impl> impl(new (std::nothrow) Impl(queueResult.TakeValue(), metrics, diagnosticsEmitter,
                                                           maxSessionCount, std::move(pendingEntries)));
        if (impl == nullptr)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoff>>::Failure(NrErrorCode::OutOfMemory);
        }

        const NrStatus initializeStatus = impl->InitializeSlotMap();
        if (initializeStatus.Failed())
        {
            return NrResult<std::unique_ptr<NrToWorldHandoff>>::Failure(initializeStatus);
        }

        std::unique_ptr<NrToWorldHandoff> handoff(new (std::nothrow) NrToWorldHandoff(std::move(impl)));
        if (handoff == nullptr)
        {
            return NrResult<std::unique_ptr<NrToWorldHandoff>>::Failure(NrErrorCode::OutOfMemory);
        }
        return NrResult<std::unique_ptr<NrToWorldHandoff>>(std::move(handoff));
    }

    NrStatus NrToWorldHandoff::ReserveSession(const NrSessionKey sessionKey) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->ReserveSession(sessionKey);
    }

    NrStatus NrToWorldHandoff::CancelSessionReservation(const NrSessionKey sessionKey) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->CancelSessionReservation(sessionKey);
    }

    NrStatus NrToWorldHandoff::RecordAccepted(const NrSessionKey sessionKey,
                                              NrSessionSendChannelControl& sendChannelControl) noexcept
    {
        if (impl_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return impl_->RecordAccepted(sessionKey, sendChannelControl);
    }

    NrStatus NrToWorldHandoff::RecordClosed(const NrSessionKey sessionKey, const NrSessionEndReason endReason) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->RecordClosed(sessionKey, endReason);
    }

    NrStatus NrToWorldHandoff::RecordPacket(const NrSessionKey sessionKey, const NrPacketType packetType,
                                            const std::span<const std::byte> payload) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->RecordPacket(sessionKey, packetType, payload);
    }

    NrStatus NrToWorldHandoff::TryPop(std::unique_ptr<NrToWorldHandoffEvent>& outEvent) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->TryPop(outEvent);
    }

    NrStatus NrToWorldHandoff::TryPopBatch(const std::span<std::unique_ptr<NrToWorldHandoffEvent>> eventBuffer,
                                           std::size_t* const outEventCount) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->TryPopBatch(eventBuffer, outEventCount);
    }

    NrStatus NrToWorldHandoff::WaitForEvents(const std::chrono::nanoseconds timeout,
                                             NrToWorldHandoffWaitResult* const outWaitResult) noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState)
                                : impl_->WaitForEvents(timeout, outWaitResult);
    }

    NrStatus NrToWorldHandoff::Close() noexcept
    {
        return impl_ == nullptr ? NrStatus::Failure(NrErrorCode::InvalidState) : impl_->Close();
    }

    std::size_t NrToWorldHandoff::ActiveSlotCount() const noexcept
    {
        if (impl_ == nullptr)
        {
            return 0;
        }
        NrScopedLock guard(impl_->lock);
        return impl_->publicationSlots.size();
    }

    std::size_t NrToWorldHandoff::PendingSlotCount() const noexcept
    {
        if (impl_ == nullptr)
        {
            return 0;
        }
        NrScopedLock guard(impl_->lock);
        return impl_->pendingSize;
    }

    NrToWorldHandoffStats NrToWorldHandoff::Stats() const noexcept
    {
        if (impl_ == nullptr)
        {
            return {};
        }

        NrScopedLock guard(impl_->lock);
        return NrToWorldHandoffStats{impl_->eventDepth, impl_->eventHighWatermark};
    }

    NrToWorldHandoff::NrToWorldHandoff(std::unique_ptr<Impl> impl) noexcept
        : impl_(std::move(impl))
    {
    }
} // namespace psnr::runtime::internal
