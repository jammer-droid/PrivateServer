#pragma once

#include "NrClientEvent.h"
#include "NrPacketType.h"
#include "NrStatus.h"

#include <cstddef>
#include <span>

namespace psnr::runtime::internal
{
    class INrClientTransportEventSink // Internal에서 DLL 쪽 event storage를 호출하기 위한 계약
    {
    public:
        INrClientTransportEventSink() noexcept = default;

        INrClientTransportEventSink(const INrClientTransportEventSink&) = delete;
        INrClientTransportEventSink& operator=(const INrClientTransportEventSink&) = delete;

        virtual ~INrClientTransportEventSink() noexcept = default;

        [[nodiscard]] virtual psnr::core::NrStatus PublishTransportConnected() noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus PublishTransportConnectionFailed(
            psnr::core::NrStatus transportStatus) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus PublishPacketReceived(
            psnr::core::NrPacketType packetType, std::span<const std::byte> payload) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus PublishTransportDisconnected(
            NrClientDisconnectReason reason, psnr::core::NrStatus transportStatus) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus HandleEventSpaceAvailable() noexcept = 0;
    };
} // namespace psnr::runtime::internal
