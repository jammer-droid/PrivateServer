#pragma once

#include "NrResult.h"
#include "NrStatus.h"

#include "NrServerIoOpInternal.h"

namespace psnr::runtime
{
    using psnr::core::NrResult;
    using psnr::core::NrStatus;

    class INrServerIoOpStateHandler
    {
    public:
        INrServerIoOpStateHandler() noexcept = default;

        INrServerIoOpStateHandler(const INrServerIoOpStateHandler&) = delete;
        INrServerIoOpStateHandler& operator=(const INrServerIoOpStateHandler&) = delete;

        virtual ~INrServerIoOpStateHandler() noexcept = default;

        [[nodiscard]] virtual NrStatus Enter() noexcept = 0;
        [[nodiscard]] virtual NrResult<NrServerIoOpPollResult> Poll() noexcept = 0;
        [[nodiscard]] virtual NrStatus Exit() noexcept = 0;
    };
} // namespace psnr::runtime
