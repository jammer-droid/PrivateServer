#include "pch.h"

#include "NrIocpIoCompletionDispatcher.h"

#include "NrAcceptIoContext.h"
#include "NrErrorCode.h"
#include "NrIocpIoContextHeader.h"
#include "NrIocpCompletionPacket.h"
#include "NrIoEvent.h"
#include "NrIoEventDispatcher.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"

#include <cstdint>

namespace psnr::runtime
{
    namespace
    {
        [[nodiscard]] std::uint32_t ToTransferredBytes(DWORD bytesTransferred) noexcept
        {
            return static_cast<std::uint32_t>(bytesTransferred);
        }
    } // namespace

    NrIocpIoCompletionDispatcher::NrIocpIoCompletionDispatcher(NrIoEventDispatcher& dispatcher) noexcept
        : dispatcher_(dispatcher)
    {
    }

    NrStatus NrIocpIoCompletionDispatcher::HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept
    {
        return Dispatch(packet);
    }

    NrStatus NrIocpIoCompletionDispatcher::HandleControlCompletion(const NrIocpCompletionPacket& packet) noexcept
    {
        static_cast<void>(packet);
        return NrStatus::Success();
    }

    NrStatus NrIocpIoCompletionDispatcher::Dispatch(const NrIocpCompletionPacket& packet) noexcept
    {
        NrIocpIoContextHeader* header = NrIocpIoContextHeader::FromOverlapped(packet.overlapped);
        if (header == nullptr)
        {
            return NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }

        const std::uint32_t bytesTransferred = ToTransferredBytes(packet.bytesTransferred);

        switch (header->Type())
        {
        case NrIoOperationType::Accept:
        {
            NrAcceptIoContext* context = NrAcceptIoContext::FromOverlapped(packet.overlapped);
            if (context == nullptr)
            {
                return NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
            }

            return dispatcher_.DispatchAccept(NrIoEvent::Accept(bytesTransferred, packet.ioStatus), *context);
        }
        case NrIoOperationType::Recv:
        {
            NrRecvIoContext* context = NrRecvIoContext::FromOverlapped(packet.overlapped);
            if (context == nullptr)
            {
                return NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
            }

            return dispatcher_.DispatchRecv(
                NrIoEvent::Recv(context->sessionKey, context->writableLength, bytesTransferred, packet.ioStatus),
                *context);
        }
        case NrIoOperationType::Send:
        {
            NrSendIoContext* context = NrSendIoContext::FromOverlapped(packet.overlapped);
            if (context == nullptr)
            {
                return NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
            }

            return dispatcher_.DispatchSend(
                NrIoEvent::Send(context->sessionKey, context->payloadLength, bytesTransferred, packet.ioStatus),
                *context);
        }
        case NrIoOperationType::Connect:
        case NrIoOperationType::Unknown:
        default:
            return NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }
    }
} // namespace psnr::runtime
