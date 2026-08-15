#pragma once

#include "Export.h"
#include "NrByteView.h"
#include "NrPacketType.h"
#include "NrSessionSendChannel.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrPacketType;
    using psnr::core::NrStatus;

    namespace internal
    {
        class NrGatewayAccess;
    }

    struct NrSessionSendChannelView final
    {
        const NrSessionSendChannel* data = nullptr;
        std::uint32_t size = 0;
    };

    struct NrGatewaySendReport final
    {
        std::uint32_t attempted = 0;
        std::uint32_t accepted = 0;
        std::uint32_t rejected = 0;
    };

    class NrGateway final
    {
    public:
        PSNR_API NrGateway() noexcept;

        NrGateway(const NrGateway&) = delete;
        NrGateway& operator=(const NrGateway&) = delete;

        PSNR_API NrGateway(NrGateway&& other) noexcept;
        PSNR_API NrGateway& operator=(NrGateway&& other) noexcept;

        PSNR_API ~NrGateway() noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API NrStatus Submit(const NrSessionSendChannel& channel,
                                               NrPacketType packetType,
                                               NrByteView payload) noexcept;
        [[nodiscard]] PSNR_API NrStatus SubmitMany(NrSessionSendChannelView channels,
                                                   NrPacketType packetType,
                                                   NrByteView payload,
                                                   NrGatewaySendReport& outReport) noexcept;

    private:
        friend class internal::NrGatewayAccess;

        struct Impl;

        explicit NrGateway(Impl* impl) noexcept;

        Impl* impl_ = nullptr;
    };
} // namespace psnr::runtime
