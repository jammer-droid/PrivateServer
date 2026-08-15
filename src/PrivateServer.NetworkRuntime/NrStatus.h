#pragma once

#include "NrErrorCode.h"

#include <cstdint>

namespace psnr::core
{

    class NrStatus
    {
    public:
        constexpr NrStatus() noexcept = default; // default status is success

        constexpr NrStatus(NrErrorCode errorCode, NrNativeErrorCode nativeErrorCode = 0) noexcept
            : errorCode_(errorCode)
            , nativeErrorCode_(nativeErrorCode)
        {
        }

        [[nodiscard]] constexpr static NrStatus Success() noexcept
        {
            return NrStatus();
        }

        [[nodiscard]] constexpr static NrStatus Failure(NrErrorCode errorCode,
                                                        NrNativeErrorCode nativeErrorCode = 0) noexcept
        {
            return NrStatus(errorCode, nativeErrorCode);
        }

        [[nodiscard]] constexpr NrErrorCode ErrorCode() const noexcept
        {
            return errorCode_;
        }

        [[nodiscard]] constexpr NrNativeErrorCode NativeErrorCode() const noexcept
        {
            return nativeErrorCode_;
        }

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return errorCode_ == NrErrorCode::Success;
        }

        [[nodiscard]] constexpr bool Failed() const noexcept
        {
            return !Succeeded();
        }

    private:
        NrErrorCode errorCode_ = NrErrorCode::Success;
        NrNativeErrorCode nativeErrorCode_ = 0;
    };
} // namespace psnr::core
