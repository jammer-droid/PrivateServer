#pragma once

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    enum class NrPressureTransactionOutcome : std::uint8_t
    {
        ToWorldPacketAdmissionRejected,
        ToWorldLifecyclePublicationDeferred,
        ActorAdmissionMailboxRejected,
        ActorAdmissionReadyCapacityRejected,
        SendAdmissionRejected,
        ReceivePressureCloseCommitted,
        SendPressureCloseCommitted,
        SendResourceAcquireFailed,
        NativeSendPostFailed,

        Count,
    };

    enum class NrServerMemoryPoolRole : std::uint8_t
    {
        RecvBuffer,
        OverlappedContext,
        RuntimeIngressQueueStorage,
        ToWorldEventQueueStorage,
        SessionAcceptRecvMailboxStorage,
        SessionSendMailboxStorage,
        ActorReadyQueueStorage,
        Payload64,
        Payload256,
        Payload1024,
        Payload8192,
        PayloadRefControl,

        Count,
    };

    enum class NrPoolPressureOutcome : std::uint8_t
    {
        Exhausted,

        Count,
    };

    inline constexpr std::size_t NrPressureTransactionOutcomeCount =
        static_cast<std::size_t>(NrPressureTransactionOutcome::Count);
    inline constexpr std::size_t NrServerMemoryPoolRoleCount = static_cast<std::size_t>(NrServerMemoryPoolRole::Count);
    inline constexpr std::size_t NrPoolPressureOutcomeCount = static_cast<std::size_t>(NrPoolPressureOutcome::Count);
} // namespace psnr::runtime
