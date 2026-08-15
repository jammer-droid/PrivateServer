#pragma once

#include "NrClientEvent.h"

#include <span>

namespace psnr::runtime::internal
{
    class NrClientEventAccess final
    {
    public:
        [[nodiscard]] static NrStatus CreateTransportConnected(NrClientEvent& outEvent) noexcept;

        [[nodiscard]] static NrStatus CreateTransportConnectionFailed(NrStatus transportStatus,
                                                                       NrClientEvent& outEvent) noexcept;

        [[nodiscard]] static NrStatus CreatePacketReceived(NrPacketType packetType,
                                                           std::span<const std::byte> payload,
                                                           NrClientEvent& outEvent) noexcept;

        [[nodiscard]] static NrStatus CreateTransportDisconnected(NrClientDisconnectReason reason,
                                                                  NrStatus transportStatus,
                                                                  NrClientEvent& outEvent) noexcept;
    };
} // namespace psnr::runtime::internal
