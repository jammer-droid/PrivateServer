#pragma once

#include "NrPayloadRef.h"
#include "NrSessionEndReason.h"
#include "NrSessionKey.h"
#include "NrStatus.h"
#include "NrTypeTraits.h"

#include <cstddef>

namespace psnr::core
{
    struct NrSessionIoCompletedEvent
    {
        NrSessionKey sessionKey = 0;
        std::size_t bytesTransferred = 0;
        NrStatus status;
        const void* contextToken = nullptr;
    };

    enum class NrSessionRecvEventType
    {
        Accepted,       // 새 Runtime Session의 World publication과 첫 recv를 시작한다.
        RecvCompleted,  // 완료된 recv IO를 session actor에서 순서대로 처리한다.
        CloseRequested, // World/application이 요청한 close를 recv FIFO 경계에서 처리한다.
    };

    struct NrSessionRecvEvent
    {
        NrSessionRecvEventType type = NrSessionRecvEventType::Accepted;
        NrSessionIoCompletedEvent completed{};
        psnr::runtime::NrSessionEndReason endReason = psnr::runtime::NrSessionEndReason::None;
    };

    struct NrSessionSendRequestedEvent
    {
        NrPayloadRef payload;
    };

    enum class NrSessionSendEventType
    {
        SendRequested,
        SendCompleted,
    };

    struct NrSessionSendEvent
    {
        NrSessionSendEventType type = NrSessionSendEventType::SendRequested;
        NrSessionSendRequestedEvent requested{};
        NrSessionIoCompletedEvent completed{};
    };

    static_assert(NrConceptObjectType<NrSessionIoCompletedEvent>);
    static_assert(NrConceptNoThrowDestructible<NrSessionIoCompletedEvent>);
    static_assert(NrConceptNoThrowMoveConstructible<NrSessionIoCompletedEvent>);
    static_assert(NrConceptNoThrowMoveAssignable<NrSessionIoCompletedEvent>);

    static_assert(NrConceptObjectType<NrSessionRecvEvent>);
    static_assert(NrConceptNoThrowDestructible<NrSessionRecvEvent>);
    static_assert(NrConceptNoThrowMoveConstructible<NrSessionRecvEvent>);
    static_assert(NrConceptNoThrowMoveAssignable<NrSessionRecvEvent>);

    static_assert(NrConceptObjectType<NrSessionSendRequestedEvent>);
    static_assert(NrConceptNoThrowDestructible<NrSessionSendRequestedEvent>);
    static_assert(NrConceptNoThrowMoveConstructible<NrSessionSendRequestedEvent>);
    static_assert(NrConceptNoThrowMoveAssignable<NrSessionSendRequestedEvent>);

    static_assert(NrConceptObjectType<NrSessionSendEvent>);
    static_assert(NrConceptNoThrowDestructible<NrSessionSendEvent>);
    static_assert(NrConceptNoThrowMoveConstructible<NrSessionSendEvent>);
    static_assert(NrConceptNoThrowMoveAssignable<NrSessionSendEvent>);
} // namespace psnr::core
