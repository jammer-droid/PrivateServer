#pragma once

#include "NrIoOperationType.h"
#include "NrSessionKey.h"

#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrSessionKey;

    class NrIoContextView final
    {
    public:
        constexpr NrIoContextView() noexcept = default;

        [[nodiscard]] static constexpr NrIoContextView Accept() noexcept
        {
            return NrIoContextView(NrIoOperationType::Accept, 0, 0);
        }

        [[nodiscard]] static constexpr NrIoContextView Recv(NrSessionKey sessionKey,
                                                            std::uint32_t payloadCapacity) noexcept
        {
            return NrIoContextView(NrIoOperationType::Recv, sessionKey, payloadCapacity);
        }

        [[nodiscard]] static constexpr NrIoContextView Send(NrSessionKey sessionKey,
                                                            std::uint32_t payloadCapacity) noexcept
        {
            return NrIoContextView(NrIoOperationType::Send, sessionKey, payloadCapacity);
        }

        [[nodiscard]] constexpr NrIoOperationType OperationType() const noexcept
        {
            return operationType_;
        }

        [[nodiscard]] constexpr NrSessionKey SessionKey() const noexcept
        {
            return sessionKey_;
        }

        [[nodiscard]] constexpr std::uint32_t PayloadCapacity() const noexcept
        {
            return payloadCapacity_;
        }

    private:
        constexpr NrIoContextView(NrIoOperationType operationType, NrSessionKey sessionKey,
                                  std::uint32_t payloadCapacity) noexcept
            : operationType_(operationType)
            , sessionKey_(sessionKey)
            , payloadCapacity_(payloadCapacity)
        {
        }

        NrIoOperationType operationType_ = NrIoOperationType::Unknown;
        NrSessionKey sessionKey_ = 0;
        std::uint32_t payloadCapacity_ = 0;
    };
} // namespace psnr::runtime
