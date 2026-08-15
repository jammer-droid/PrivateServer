#pragma once

#include "NrSessionSendChannel.h"

namespace psnr::runtime
{
    struct NrSessionSendChannelControl;

    namespace internal
    {
        class NrSessionSendChannelAccess final
        {
        public:
            [[nodiscard]] static NrSessionSendChannel CreatePublicChannel(
                NrSessionSendChannelControl& control) noexcept;
            [[nodiscard]] static NrSessionSendChannelControl* GetControl(const NrSessionSendChannel& channel) noexcept;
        };
    } // namespace internal
} // namespace psnr::runtime
