#pragma once

#include "NrDiagnosticEmitter.h"
#include "NrPacketType.h"
#include "NrResult.h"
#include "NrSessionEndReason.h"
#include "NrSessionKey.h"
#include "NrSessionSendChannelControl.h"
#include "NrStatus.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace psnr::core
{
    class NrMemoryPoolManager;
}

namespace psnr::runtime::internal
{
    class NrServerMetrics;

    enum class NrToWorldHandoffEventKind : std::uint8_t
    {
        None,
        SessionAccepted,
        PacketReceived,
        SessionClosed,
    };

    class NrToWorldHandoffEvent final
    {
    public:
        NrToWorldHandoffEvent(const NrToWorldHandoffEvent&) = delete;
        NrToWorldHandoffEvent& operator=(const NrToWorldHandoffEvent&) = delete;

        NrToWorldHandoffEvent(NrToWorldHandoffEvent&&) = delete;
        NrToWorldHandoffEvent& operator=(NrToWorldHandoffEvent&&) = delete;

        ~NrToWorldHandoffEvent() noexcept = default;

        [[nodiscard]] static psnr::core::NrResult<std::unique_ptr<NrToWorldHandoffEvent>> CreateSessionAccepted(
            psnr::core::NrSessionKey sessionKey, NrSessionSendChannelControl& sendChannelControl) noexcept;
        [[nodiscard]] static psnr::core::NrResult<std::unique_ptr<NrToWorldHandoffEvent>> CreatePacketReceived(
            psnr::core::NrSessionKey sessionKey, psnr::core::NrPacketType packetType,
            std::span<const std::byte> payload) noexcept;
        [[nodiscard]] static psnr::core::NrResult<std::unique_ptr<NrToWorldHandoffEvent>> CreateSessionClosed(
            psnr::core::NrSessionKey sessionKey, NrSessionEndReason endReason) noexcept;

        [[nodiscard]] NrToWorldHandoffEventKind Kind() const noexcept;
        [[nodiscard]] psnr::core::NrSessionKey SessionKey() const noexcept;
        [[nodiscard]] NrSessionSendChannelControl* SendChannelControl() const noexcept;
        [[nodiscard]] psnr::core::NrPacketType PacketType() const noexcept;
        [[nodiscard]] std::span<const std::byte> Payload() const noexcept;
        [[nodiscard]] NrSessionEndReason EndReason() const noexcept;

    private:
        NrToWorldHandoffEvent(NrToWorldHandoffEventKind kind, psnr::core::NrSessionKey sessionKey) noexcept;

        NrToWorldHandoffEventKind kind_ = NrToWorldHandoffEventKind::None;
        psnr::core::NrSessionKey sessionKey_ = 0;
        NrSessionSendChannelControlHandle sendChannelControl_;
        psnr::core::NrPacketType packetType_{};
        std::unique_ptr<std::byte[]> payload_;
        std::uint32_t payloadLength_ = 0;
        NrSessionEndReason endReason_ = NrSessionEndReason::None;
    };

    struct NrToWorldHandoffStats final
    {
        std::size_t eventDepth = 0;
        std::size_t eventHighWatermark = 0;
    };

    enum class NrToWorldHandoffWaitResult : std::uint8_t
    {
        EventsAvailable = 0,
        TimedOut,
        Closed,
    };

    class NrToWorldHandoff final
    {
    public:
        NrToWorldHandoff(const NrToWorldHandoff&) = delete;
        NrToWorldHandoff& operator=(const NrToWorldHandoff&) = delete;
        NrToWorldHandoff(NrToWorldHandoff&&) = delete;
        NrToWorldHandoff& operator=(NrToWorldHandoff&&) = delete;

        ~NrToWorldHandoff() noexcept;

        [[nodiscard]] static psnr::core::NrResult<std::unique_ptr<NrToWorldHandoff>> Create(
            psnr::core::NrMemoryPoolManager& memoryPoolManager, NrServerMetrics& metrics,
            NrDiagnosticEmitter diagnosticsEmitter, std::size_t maxSessionCount,
            std::size_t eventQueueCapacity) noexcept;

        [[nodiscard]] psnr::core::NrStatus ReserveSession(psnr::core::NrSessionKey sessionKey) noexcept;
        [[nodiscard]] psnr::core::NrStatus CancelSessionReservation(psnr::core::NrSessionKey sessionKey) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordAccepted(psnr::core::NrSessionKey sessionKey,
                                                          NrSessionSendChannelControl& sendChannelControl) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordPacket(psnr::core::NrSessionKey sessionKey,
                                                        psnr::core::NrPacketType packetType,
                                                        std::span<const std::byte> payload) noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordClosed(psnr::core::NrSessionKey sessionKey,
                                                        NrSessionEndReason endReason) noexcept;
        [[nodiscard]] psnr::core::NrStatus TryPop(std::unique_ptr<NrToWorldHandoffEvent>& outEvent) noexcept;
        [[nodiscard]] psnr::core::NrStatus TryPopBatch(std::span<std::unique_ptr<NrToWorldHandoffEvent>> eventBuffer,
                                                       std::size_t* outEventCount) noexcept;
        [[nodiscard]] psnr::core::NrStatus WaitForEvents(std::chrono::nanoseconds timeout,
                                                         NrToWorldHandoffWaitResult* outWaitResult) noexcept;
        [[nodiscard]] psnr::core::NrStatus Close() noexcept;

        [[nodiscard]] std::size_t ActiveSlotCount() const noexcept;
        [[nodiscard]] std::size_t PendingSlotCount() const noexcept;
        [[nodiscard]] NrToWorldHandoffStats Stats() const noexcept;

    private:
        struct Impl;

        explicit NrToWorldHandoff(std::unique_ptr<Impl> impl) noexcept;

        std::unique_ptr<Impl> impl_;
    };
} // namespace psnr::runtime::internal
