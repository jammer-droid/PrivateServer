#include "pch.h"

#include "NrToWorldEvent.h"

#include "NrToWorldEventAccess.h"

#include "NrErrorCode.h"
#include "NrSessionSendChannelAccess.h"
#include "NrToWorldHandoff.h"

#include <memory>
#include <span>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrToWorldEvent::NrToWorldEvent() noexcept = default;

    NrToWorldEvent::NrToWorldEvent(NrToWorldEvent&& other) noexcept
        : event_(std::exchange(other.event_, nullptr))
    {
    }

    NrToWorldEvent& NrToWorldEvent::operator=(NrToWorldEvent&& other) noexcept
    {
        if (this != &other)
        {
            delete event_;
            event_ = std::exchange(other.event_, nullptr);
        }

        return *this;
    }

    NrToWorldEvent::~NrToWorldEvent() noexcept
    {
        delete event_;
        event_ = nullptr;
    }

    bool NrToWorldEvent::IsValid() const noexcept
    {
        return event_ != nullptr;
    }

    NrToWorldEventKind NrToWorldEvent::Kind() const noexcept
    {
        if (event_ == nullptr)
        {
            return NrToWorldEventKind::None;
        }

        switch (event_->Kind())
        {
        case internal::NrToWorldHandoffEventKind::SessionAccepted:
            return NrToWorldEventKind::SessionAccepted;
        case internal::NrToWorldHandoffEventKind::PacketReceived:
            return NrToWorldEventKind::PacketReceived;
        case internal::NrToWorldHandoffEventKind::SessionClosed:
            return NrToWorldEventKind::SessionClosed;
        case internal::NrToWorldHandoffEventKind::None:
            return NrToWorldEventKind::None;
        }

        return NrToWorldEventKind::None;
    }

    NrSessionKey NrToWorldEvent::SessionKey() const noexcept
    {
        return event_ == nullptr ? 0 : event_->SessionKey();
    }

    NrStatus NrToWorldEvent::GetSendChannel(NrSessionSendChannel* const outChannel) const noexcept
    {
        if (outChannel == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
        if (event_ == nullptr || event_->Kind() != internal::NrToWorldHandoffEventKind::SessionAccepted)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrSessionSendChannelControl* sendChannelControl = event_->SendChannelControl();
        if (sendChannelControl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        *outChannel = internal::NrSessionSendChannelAccess::CreatePublicChannel(*sendChannelControl);
        return NrStatus::Success();
    }

    NrStatus NrToWorldEvent::GetPacketType(NrPacketType* const outPacketType) const noexcept
    {
        if (outPacketType == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
        if (event_ == nullptr || event_->Kind() != internal::NrToWorldHandoffEventKind::PacketReceived)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        *outPacketType = event_->PacketType();
        return NrStatus::Success();
    }

    NrStatus NrToWorldEvent::GetPayload(NrByteView* const outPayload) const noexcept
    {
        if (outPayload == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }
        if (event_ == nullptr || event_->Kind() != internal::NrToWorldHandoffEventKind::PacketReceived)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const std::span<const std::byte> payload = event_->Payload();
        *outPayload = NrByteView{payload.data(), static_cast<std::uint32_t>(payload.size())};
        return NrStatus::Success();
    }

    NrStatus NrToWorldEvent::GetEndReason(NrSessionEndReason& outReason) const noexcept
    {
        if (event_ == nullptr || event_->Kind() != internal::NrToWorldHandoffEventKind::SessionClosed)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        outReason = event_->EndReason();
        return NrStatus::Success();
    }

    NrToWorldEvent::NrToWorldEvent(internal::NrToWorldHandoffEvent* event) noexcept
        : event_(event)
    {
    }

    NrStatus internal::NrToWorldEventAccess::Adopt(std::unique_ptr<NrToWorldHandoffEvent> event,
                                                   NrToWorldEvent& outEvent) noexcept
    {
        if (event == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (outEvent.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        outEvent = NrToWorldEvent(event.release());
        return NrStatus::Success();
    }
} // namespace psnr::runtime
