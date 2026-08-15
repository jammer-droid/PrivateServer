#include "pch.h"

#include "NrClientIoCompletionDispatcher.h"

#include "NrClientConnectIoContext.h"
#include "NrClientIoContextOperations.h"
#include "NrErrorCode.h"
#include "NrIocpIoContextHeader.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    NrClientIoCompletionDispatcher::NrClientIoCompletionDispatcher(INrClientIoCompletionTarget& target) noexcept
        : target_(target)
    {
    }

    NrStatus NrClientIoCompletionDispatcher::HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept
    {
        NrIocpIoContextHeader* header = NrIocpIoContextHeader::FromOverlapped(packet.overlapped);
        if (header == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        switch (header->Type())
        {
        case NrIoOperationType::Connect:
        {
            NrClientConnectIoContext* context = NrClientConnectIoContext::FromOverlapped(packet.overlapped);
            if (context == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            return RouteByGeneration(NrIoOperationType::Connect, context->AttemptGeneration(), packet);
        }
        case NrIoOperationType::Recv:
        {
            NrRecvIoContext* context = NrRecvIoContext::FromOverlapped(packet.overlapped);
            if (context == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            return RouteByGeneration(NrIoOperationType::Recv, NrClientIoContextOperations::AttemptGeneration(*context),
                                     packet);
        }
        case NrIoOperationType::Send:
        {
            NrSendIoContext* context = NrSendIoContext::FromOverlapped(packet.overlapped);
            if (context == nullptr)
            {
                return NrStatus::Failure(NrErrorCode::InvalidArgument);
            }

            return RouteByGeneration(NrIoOperationType::Send, NrClientIoContextOperations::AttemptGeneration(*context),
                                     packet);
        }
        case NrIoOperationType::Accept:
        case NrIoOperationType::Unknown:
        default:
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
    }

    NrStatus NrClientIoCompletionDispatcher::HandleControlCompletion(const NrIocpCompletionPacket& packet) noexcept
    {
        NrClientControlCompletionKind kind = NrClientControlCompletionKind::None;
        const NrStatus decodeStatus = DecodeClientControlCompletion(packet, kind);
        if (decodeStatus.Failed())
        {
            return decodeStatus;
        }

        return target_.HandleClientControlCompletion(kind);
    }

    NrStatus NrClientIoCompletionDispatcher::RouteByGeneration(const NrIoOperationType operationType,
                                                               const std::uint64_t attemptGeneration,
                                                               const NrIocpCompletionPacket& packet) noexcept
    {
        if (attemptGeneration == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (attemptGeneration != target_.CurrentAttemptGeneration())
        {
            return target_.HandleStaleIoCompletion(operationType, attemptGeneration, packet);
        }

        switch (operationType)
        {
        case NrIoOperationType::Connect:
        {
            NrClientConnectIoContext* context = NrClientConnectIoContext::FromOverlapped(packet.overlapped);
            return context == nullptr ? NrStatus::Failure(NrErrorCode::InvalidArgument)
                                      : target_.HandleConnectCompletion(*context, packet);
        }
        case NrIoOperationType::Recv:
        {
            NrRecvIoContext* context = NrRecvIoContext::FromOverlapped(packet.overlapped);
            return context == nullptr ? NrStatus::Failure(NrErrorCode::InvalidArgument)
                                      : target_.HandleRecvCompletion(*context, packet);
        }
        case NrIoOperationType::Send:
        {
            NrSendIoContext* context = NrSendIoContext::FromOverlapped(packet.overlapped);
            return context == nullptr ? NrStatus::Failure(NrErrorCode::InvalidArgument)
                                      : target_.HandleSendCompletion(*context, packet);
        }
        case NrIoOperationType::Accept:
        case NrIoOperationType::Unknown:
        default:
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
    }
} // namespace psnr::runtime::internal
