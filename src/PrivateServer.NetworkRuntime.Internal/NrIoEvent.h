#pragma once

#include "NrIoContextView.h"
#include "NrSessionKey.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrSessionKey;
    using psnr::core::NrStatus;

    class NrIoEvent final
    {
    public:
        constexpr NrIoEvent() noexcept = default;

        [[nodiscard]] static constexpr NrIoEvent Accept(std::uint32_t bytesTransferred,
                                                        NrStatus status = NrStatus::Success()) noexcept
        {
            return NrIoEvent(NrIoContextView::Accept(), bytesTransferred, status);
        }

        [[nodiscard]] static constexpr NrIoEvent Recv(NrSessionKey sessionKey, std::uint32_t payloadCapacity,
                                                      std::uint32_t bytesTransferred,
                                                      NrStatus status = NrStatus::Success()) noexcept
        {
            return NrIoEvent(NrIoContextView::Recv(sessionKey, payloadCapacity), bytesTransferred, status);
        }

        [[nodiscard]] static constexpr NrIoEvent Send(NrSessionKey sessionKey, std::uint32_t payloadCapacity,
                                                      std::uint32_t bytesTransferred,
                                                      NrStatus status = NrStatus::Success()) noexcept
        {
            return NrIoEvent(NrIoContextView::Send(sessionKey, payloadCapacity), bytesTransferred, status);
        }

        constexpr NrIoEvent(NrIoContextView contextView, std::uint32_t bytesTransferred,
                            NrStatus status = NrStatus::Success()) noexcept
            : contextView_(contextView)
            , bytesTransferred_(bytesTransferred)
            , status_(status)
        {
        }

        [[nodiscard]] constexpr NrIoOperationType OperationType() const noexcept
        {
            return contextView_.OperationType();
        }

        [[nodiscard]] constexpr NrSessionKey SessionKey() const noexcept
        {
            return contextView_.SessionKey();
        }

        [[nodiscard]] constexpr std::uint32_t BytesTransferred() const noexcept
        {
            return bytesTransferred_;
        }

        [[nodiscard]] constexpr NrStatus Status() const noexcept
        {
            return status_;
        }

        [[nodiscard]] constexpr const NrIoContextView& ContextView() const noexcept
        {
            return contextView_;
        }

    private:
        NrIoContextView contextView_;
        std::uint32_t bytesTransferred_ = 0;
        NrStatus status_ = NrStatus::Success();
    };
} // namespace psnr::runtime
