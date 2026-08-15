#include "pch.h"

#include "NrClientControlCompletion.h"

#include "NrErrorCode.h"

namespace psnr::runtime::internal
{
    psnr::core::NrStatus PostClientControlCompletion(NrIocpPort& iocpPort,
                                                     const NrClientControlCompletionKind kind) noexcept
    {
        if (kind == NrClientControlCompletionKind::None)
        {
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }

        return iocpPort.PostControlCompletion(static_cast<std::uintptr_t>(kind));
    }

    psnr::core::NrStatus DecodeClientControlCompletion(const NrIocpCompletionPacket& packet,
                                                       NrClientControlCompletionKind& outKind) noexcept
    {
        if (packet.overlapped != nullptr)
        {
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }

        const NrClientControlCompletionKind kind = static_cast<NrClientControlCompletionKind>(packet.completionKey);
        switch (kind)
        {
        case NrClientControlCompletionKind::EventSpaceAvailable:
        case NrClientControlCompletionKind::Stop:
        case NrClientControlCompletionKind::CommandsReady:
            outKind = kind;
            return psnr::core::NrStatus::Success();

        case NrClientControlCompletionKind::None:
        default:
            return psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidArgument);
        }
    }
} // namespace psnr::runtime::internal
