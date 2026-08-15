#pragma once

#include "Export.h"
#include "NrByteView.h"
#include "NrPacketType.h"
#include "NrSessionEndReason.h"
#include "NrSessionKey.h"
#include "NrSessionSendChannel.h"
#include "NrStatus.h"

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrPacketType;
    using psnr::core::NrSessionKey;
    using psnr::core::NrStatus;

    inline constexpr std::size_t NrMaxToWorldEventBatchSize = 128;

    namespace internal
    {
        class NrToWorldEventAccess;
        class NrToWorldHandoffEvent;
    } // namespace internal

    enum class NrToWorldEventKind : std::uint8_t
    {
        None,
        SessionAccepted,
        PacketReceived,
        SessionClosed,
    };

    class NrToWorldEvent final
    {
    public:
        PSNR_API NrToWorldEvent() noexcept;

        NrToWorldEvent(const NrToWorldEvent&) = delete;
        NrToWorldEvent& operator=(const NrToWorldEvent&) = delete;

        PSNR_API NrToWorldEvent(NrToWorldEvent&& other) noexcept;
        PSNR_API NrToWorldEvent& operator=(NrToWorldEvent&& other) noexcept;

        PSNR_API ~NrToWorldEvent() noexcept;

        [[nodiscard]] PSNR_API bool IsValid() const noexcept;
        [[nodiscard]] PSNR_API NrToWorldEventKind Kind() const noexcept;
        [[nodiscard]] PSNR_API NrSessionKey SessionKey() const noexcept;

        [[nodiscard]] PSNR_API NrStatus GetSendChannel(NrSessionSendChannel* outChannel) const noexcept;
        [[nodiscard]] PSNR_API NrStatus GetPacketType(NrPacketType* outPacketType) const noexcept;
        // 반환 view는 이 event가 payload owner로 살아 있는 동안만 유효하다.
        [[nodiscard]] PSNR_API NrStatus GetPayload(NrByteView* outPayload) const noexcept;
        [[nodiscard]] PSNR_API NrStatus GetEndReason(NrSessionEndReason& outReason) const noexcept;

    private:
        friend class internal::NrToWorldEventAccess;

        explicit NrToWorldEvent(internal::NrToWorldHandoffEvent* event) noexcept;

        internal::NrToWorldHandoffEvent* event_ = nullptr;
    };
} // namespace psnr::runtime
