#pragma once

#include "Export.h"
#include "NrByteView.h"
#include "NrPacketType.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrPacketType;
    using psnr::core::NrStatus;

    namespace internal
    {
        class NrClientEventAccess;
    } // namespace internal

    enum class NrClientEventKind : std::uint8_t
    {
        None,
        TransportConnected,
        TransportConnectionFailed,
        PacketReceived,
        TransportDisconnected,
    };

    enum class NrClientDisconnectReason : std::uint8_t
    {
        None,
        LocalRequested,  // outer owner
        RemoteClosed,    // worker
        ReceivePressure, // worker
        TransportError,  // worker
        ProtocolError,   // worker
    };

    class NrClientEvent final
    {
    public:
        PSNR_API NrClientEvent() noexcept;

        NrClientEvent(const NrClientEvent&) = delete;
        NrClientEvent& operator=(const NrClientEvent&) = delete;

        PSNR_API NrClientEvent(NrClientEvent&& other) noexcept;
        PSNR_API NrClientEvent& operator=(NrClientEvent&& other) noexcept;

        PSNR_API ~NrClientEvent() noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API NrClientEventKind Kind() const noexcept;

        [[nodiscard]] PSNR_API NrStatus GetPacketType(NrPacketType* outPacketType) const noexcept;
        [[nodiscard]] PSNR_API NrStatus GetPayload(NrByteView* outPayload) const noexcept;
        [[nodiscard]] PSNR_API NrStatus GetTransportStatus(NrStatus* outStatus) const noexcept;
        [[nodiscard]] PSNR_API NrStatus GetDisconnectReason(NrClientDisconnectReason* outReason) const noexcept;

    private:
        friend class internal::NrClientEventAccess;

        struct Impl;

        explicit NrClientEvent(Impl* impl) noexcept;

        Impl* impl_ = nullptr;
    };
} // namespace psnr::runtime
