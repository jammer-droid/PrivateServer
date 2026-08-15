#pragma once

#include "NrErrorCode.h"
#include "NrSessionEndReason.h"
#include "NrSessionKey.h"
#include "NrTypeTraits.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    enum class NrDiagnosticSeverity : std::uint8_t
    {
        Unknown = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
    };

    enum class NrDiagnosticEventKind : std::uint8_t
    {
        Unknown = 0,
        Transition = 1,
        Failure = 2,
        Anomaly = 3,
    };

    enum class NrDiagnosticComponent : std::uint16_t
    {
        Unknown = 0,
        ServerLifecycle = 1,
        IoPipeline = 2,
        Session = 3,
        ActorScheduler = 4,
        ToWorldHandoff = 5,
        MemoryPool = 6,
    };

    enum class NrDiagnosticOperation : std::uint16_t
    {
        Unknown = 0,
        Configure = 1,
        Start = 2,
        RequestStop = 3,
        Shutdown = 4,
        Accept = 5,
        Receive = 6,
        Send = 7,
        Close = 8,
        Admission = 9,
        Acquire = 10,
        Post = 11,
        Complete = 12,
    };

    enum class NrDiagnosticContextFlags : std::uint8_t
    {
        None = 0,
        HasSessionKey = 1 << 0,
        HasIoOperation = 1 << 1,
        HasCloseReason = 1 << 2,
    };

    enum class NrDiagnosticIoOperation : std::uint8_t
    {
        Unknown = 0,
        Accept = 1,
        Receive = 2,
        Send = 3,
    };

    struct NrDiagnosticRecord final
    {
        std::uint64_t producerTimestamp = 0;
        std::uint64_t drainSequence = 0;
        psnr::core::NrSessionKey sessionKey = 0;
        psnr::core::NrErrorCode errorCode = psnr::core::NrErrorCode::Success;
        psnr::core::NrNativeErrorCode nativeErrorCode = 0;
        NrDiagnosticComponent component = NrDiagnosticComponent::Unknown;
        NrDiagnosticOperation operation = NrDiagnosticOperation::Unknown;
        NrDiagnosticSeverity severity = NrDiagnosticSeverity::Unknown;
        NrDiagnosticEventKind eventKind = NrDiagnosticEventKind::Unknown;
        NrDiagnosticContextFlags contextFlags = NrDiagnosticContextFlags::None;
        NrDiagnosticIoOperation ioOperation = NrDiagnosticIoOperation::Unknown;
        NrSessionEndReason closeReason = NrSessionEndReason::None;
    };

    static_assert(sizeof(psnr::core::NrErrorCode) == sizeof(std::uint32_t));
    static_assert(sizeof(NrDiagnosticRecord) == 48);
    static_assert(alignof(NrDiagnosticRecord) == alignof(std::uint64_t));
    static_assert(psnr::core::NrConceptTriviallyCopyable<NrDiagnosticRecord>);
} // namespace psnr::runtime::internal
