#pragma once

#include "NrSessionEndReason.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    enum class NrToWorldLifecycleNotificationKind : std::uint8_t
    {
        None,            // 현재 publication slot에서 queue로 보낼 lifecycle event가 없음.
        SessionAccepted, // World가 session identity와 send capability를 먼저 관측하도록 Accepted event를 publish.
        SessionClosed,   // Accepted가 commit된 뒤 최종 end reason을 담은 Closed event를 publish.
    };

    class NrToWorldLifecycleState final
    {
    public:
        [[nodiscard]] psnr::core::NrStatus RecordAccepted() noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordClosed(NrSessionEndReason reason) noexcept;
        [[nodiscard]] psnr::core::NrStatus CommitNextPending() noexcept;

        [[nodiscard]] NrToWorldLifecycleNotificationKind NextPendingKind() const noexcept;
        [[nodiscard]] bool CanPublishPacket() const noexcept;
        [[nodiscard]] bool IsSessionClosedPublished() const noexcept;
        [[nodiscard]] psnr::core::NrStatus GetEndReason(NrSessionEndReason& outReason) const noexcept;

    private:
        NrSessionEndReason endReason_ = NrSessionEndReason::None;
        bool acceptedRecorded_ = false;  // runtime 에서 session accept 전환이 발생했는지 여부
        bool acceptedCommitted_ = false; // SessionAccepted event가 World-facing handoff queue에 commit됐는지 여부
        bool closedRecorded_ = false;    // runtime 에서 session close 전환이 발생했는지 여부
        bool closedCommitted_ = false;   // SessionClosed event가 World-facing handoff queue에 commit됐는지 여부
    };
} // namespace psnr::runtime::internal
