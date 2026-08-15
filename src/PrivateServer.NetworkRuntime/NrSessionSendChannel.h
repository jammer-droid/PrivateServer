#pragma once

#include "Export.h"
#include "NrStatus.h"
#include "NrTypeTraits.h"

namespace psnr::runtime
{
    struct NrSessionSendChannelControl;

    namespace internal
    {
        class NrSessionSendChannelAccess;
    } // namespace internal

    class NrSessionSendChannel final
    {
    public:
        NrSessionSendChannel() noexcept = default;

        PSNR_API NrSessionSendChannel(const NrSessionSendChannel& other) noexcept;
        PSNR_API NrSessionSendChannel& operator=(const NrSessionSendChannel& other) noexcept;

        PSNR_API NrSessionSendChannel(NrSessionSendChannel&& other) noexcept;
        PSNR_API NrSessionSendChannel& operator=(NrSessionSendChannel&& other) noexcept;

        PSNR_API ~NrSessionSendChannel() noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API bool IsOpen() const noexcept;

    private:
        friend class internal::NrSessionSendChannelAccess;

        explicit NrSessionSendChannel(NrSessionSendChannelControl* control) noexcept;

        void Reset() noexcept;

        NrSessionSendChannelControl* control_ = nullptr;
    };

    static_assert(psnr::core::NrConceptObjectType<NrSessionSendChannel>);
    static_assert(psnr::core::NrConceptNoThrowDestructible<NrSessionSendChannel>);
    static_assert(psnr::core::NrConceptNoThrowMoveConstructible<NrSessionSendChannel>);
    static_assert(psnr::core::NrConceptNoThrowMoveAssignable<NrSessionSendChannel>);

} // namespace psnr::runtime
