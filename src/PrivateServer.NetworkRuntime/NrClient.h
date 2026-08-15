#pragma once

#include "Export.h"
#include "NrByteView.h"
#include "NrClientConfig.h"
#include "NrClientEvent.h"
#include "NrClientSnapshot.h"
#include "NrEndpoint.h"
#include "NrPacketType.h"
#include "NrStatus.h"

namespace psnr::runtime
{
    using psnr::core::NrPacketType;
    using psnr::core::NrStatus;

    class NrClient final
    {
    public:
        PSNR_API NrClient() noexcept;

        NrClient(const NrClient&) = delete;
        NrClient& operator=(const NrClient&) = delete;

        PSNR_API NrClient(NrClient&& other) noexcept;
        PSNR_API NrClient& operator=(NrClient&& other) noexcept;

        PSNR_API ~NrClient() noexcept;

        [[nodiscard]] static PSNR_API NrStatus Create(const NrClientConfig& config, NrClient* outClient) noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;

        [[nodiscard]] PSNR_API NrStatus Connect(const NrEndpoint& endpoint) noexcept;
        [[nodiscard]] PSNR_API NrStatus Disconnect() noexcept;
        [[nodiscard]] PSNR_API NrStatus Shutdown() noexcept;

        [[nodiscard]] PSNR_API NrStatus Send(NrPacketType packetType, NrByteView payload) noexcept;

        [[nodiscard]] PSNR_API NrStatus TryPopEvent(NrClientEvent* outEvent) noexcept;
        [[nodiscard]] PSNR_API NrStatus CaptureSnapshot(NrClientSnapshot* outSnapshot) const noexcept;

    private:
        struct Impl;

        explicit NrClient(Impl* impl) noexcept;

        Impl* impl_ = nullptr;
    };
} // namespace psnr::runtime
