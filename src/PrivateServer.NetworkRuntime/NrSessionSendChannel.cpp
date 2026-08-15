#include "pch.h"

#include "NrSessionSendChannelAccess.h"
#include "NrSessionSendChannelControl.h"

#include <utility>

namespace psnr::runtime
{
    NrSessionSendChannel::NrSessionSendChannel(const NrSessionSendChannel& other) noexcept
        : control_(other.control_)
    {
        if (control_ != nullptr)
        {
            control_->AddRef();
        }
    }

    NrSessionSendChannel& NrSessionSendChannel::operator=(const NrSessionSendChannel& other) noexcept
    {
        if (this != &other)
        {
            NrSessionSendChannelControl* nextControl = other.control_;
            if (nextControl != nullptr)
            {
                nextControl->AddRef();
            }

            Reset();
            control_ = nextControl;
        }

        return *this;
    }

    NrSessionSendChannel::NrSessionSendChannel(NrSessionSendChannel&& other) noexcept
        : control_(std::exchange(other.control_, nullptr))
    {
    }

    NrSessionSendChannel& NrSessionSendChannel::operator=(NrSessionSendChannel&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            control_ = std::exchange(other.control_, nullptr);
        }

        return *this;
    }

    NrSessionSendChannel::~NrSessionSendChannel() noexcept
    {
        Reset();
    }

    bool NrSessionSendChannel::IsValid() const noexcept
    {
        return control_ != nullptr;
    }

    bool NrSessionSendChannel::IsOpen() const noexcept
    {
        return control_ != nullptr && control_->IsOpen();
    }

    NrSessionSendChannel::NrSessionSendChannel(NrSessionSendChannelControl* control) noexcept
        : control_(control)
    {
    }

    void NrSessionSendChannel::Reset() noexcept
    {
        NrSessionSendChannelControl* control = std::exchange(control_, nullptr);
        if (control != nullptr)
        {
            control->ReleaseRef();
        }
    }

    NrSessionSendChannel internal::NrSessionSendChannelAccess::CreatePublicChannel(
        NrSessionSendChannelControl& control) noexcept
    {
        control.AddRef();
        return NrSessionSendChannel(&control);
    }

    NrSessionSendChannelControl* internal::NrSessionSendChannelAccess::GetControl(
        const NrSessionSendChannel& channel) noexcept
    {
        return channel.control_;
    }

} // namespace psnr::runtime
