#include "pch.h"

#include "NrSessionIoActor.h"

#include "NrSession.h"
#include "NrToWorldHandoff.h"

#include <cassert>

namespace psnr::core
{
    NrStatus NrSessionIoActor::HandleAccepted() noexcept
    {
        assert(session_ != nullptr);
        assert(ioOperations_ != nullptr);

        assert(recvDrainDependencies_.toWorldHandoff != nullptr);
        assert(sendChannelControl_.Get() != nullptr);

        // Accepted를 먼저 기록해야 첫 PostRecv 실패도 World에서 Accepted -> Closed 순서로 관측한다.
        const NrStatus acceptedStatus =
            recvDrainDependencies_.toWorldHandoff->RecordAccepted(session_->SessionKey(), *sendChannelControl_.Get());
        if (acceptedStatus.Failed())
        {
            FailCloseAcceptedSession();
            return acceptedStatus;
        }

        const NrStatus recvStatus = PostRequestedRecv();
        if (recvStatus.Failed())
        {
            FailCloseAcceptedSession();
            return recvStatus;
        }

        return NrStatus::Success();
    }

    void NrSessionIoActor::FailCloseAcceptedSession() noexcept
    {
        RecordCloseRequested(psnr::runtime::NrSessionEndReason::TransportError);
    }
} // namespace psnr::core
