#pragma once

#include <cstdint>

namespace psnr::core
{

    enum class NrErrorCode
    {
        Success,
        InvalidArgument,
        InvalidState,
        OutOfMemory,
        PoolExhausted,
        CapacityExceeded,

        // queue
        QueueFull,
        QueueEmpty,

        // dispatch
        DispatchRuleNotFound,

        IoFailed,
        OperationCanceled,
        ProtocolError,
    };

    using NrNativeErrorCode = std::uint32_t; // Windows Native Error

} // namespace psnr::core
