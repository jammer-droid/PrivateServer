#include "pch.h"

#include "NrGateway.h"
#include "NrGatewayAccess.h"

#include "NrErrorCode.h"
#include "NrPacketHeader.h"
#include "NrPayloadRef.h"
#include "NrSessionSendChannelAccess.h"
#include "NrSessionSendChannelControl.h"

#include <new>
#include <span>
#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrPayloadRef;
    using psnr::core::NrResult;

    struct NrGateway::Impl
    {
        Impl(internal::NrSubmissionAdmissionHandle ownerSubmissionAdmission,
             psnr::core::NrMemoryPoolManager& ownerMemoryPoolManager) noexcept
            : submissionAdmission(std::move(ownerSubmissionAdmission))
            , memoryPoolManager(&ownerMemoryPoolManager)
        {
        }

        internal::NrSubmissionAdmissionHandle submissionAdmission;
        psnr::core::NrMemoryPoolManager* memoryPoolManager = nullptr;
    };

    NrGateway::NrGateway() noexcept {}

    NrGateway::NrGateway(Impl* impl) noexcept
        : impl_(impl)
    {
    }

    NrGateway::NrGateway(NrGateway&& other) noexcept
        : impl_(std::exchange(other.impl_, nullptr))
    {
    }

    NrGateway& NrGateway::operator=(NrGateway&& other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }

        return *this;
    }

    NrGateway::~NrGateway() noexcept
    {
        delete impl_;
        impl_ = nullptr;
    }

    NrStatus internal::NrGatewayAccess::Create(internal::NrSubmissionAdmissionHandle submissionAdmission,
                                               psnr::core::NrMemoryPoolManager& memoryPoolManager,
                                               NrGateway& outGateway) noexcept
    {
        if (!submissionAdmission.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (outGateway.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrGateway::Impl* impl =
            new (std::nothrow) NrGateway::Impl(std::move(submissionAdmission), memoryPoolManager);
        if (impl == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::OutOfMemory);
        }

        outGateway = NrGateway(impl);
        return NrStatus::Success();
    }

    bool NrGateway::IsValid() const noexcept
    {
        return impl_ != nullptr;
    }

    NrStatus NrGateway::Submit(const NrSessionSendChannel& channel, const NrPacketType packetType,
                               const NrByteView payload) noexcept
    {
        if (impl_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        internal::NrSubmissionPermit submissionPermit;
        const NrStatus permitStatus = impl_->submissionAdmission.TryAcquirePermit(submissionPermit);
        if (permitStatus.Failed())
        {
            return permitStatus;
        }

        if ((payload.size > 0 && payload.data == nullptr) ||
            payload.size > psnr::core::NrMaxPacketLength - psnr::core::NrPacketHeaderLength)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<NrPayloadRef> payloadResult = psnr::core::NrPayloadRefFactory::CreateFramedPayloadRef(
            *impl_->memoryPoolManager, packetType, std::span<const std::byte>(payload.data, payload.size));
        if (payloadResult.Failed())
        {
            return payloadResult.Status();
        }

        NrSessionSendChannelControl* control = internal::NrSessionSendChannelAccess::GetControl(channel);
        if (control == nullptr || !channel.IsOpen())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return control->EnqueueSendRequested(payloadResult.TakeValue(), submissionPermit);
    }

    NrStatus NrGateway::SubmitMany(const NrSessionSendChannelView channels, const NrPacketType packetType,
                                   const NrByteView payload, NrGatewaySendReport& outReport) noexcept
    {
        if (impl_ == nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        internal::NrSubmissionPermit submissionPermit;
        const NrStatus permitStatus = impl_->submissionAdmission.TryAcquirePermit(submissionPermit);
        if (permitStatus.Failed())
        {
            return permitStatus;
        }

        if ((channels.size > 0 && channels.data == nullptr) || (payload.size > 0 && payload.data == nullptr) ||
            payload.size > psnr::core::NrMaxPacketLength - psnr::core::NrPacketHeaderLength)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        NrGatewaySendReport report;
        if (channels.size == 0)
        {
            outReport = report;
            return NrStatus::Success();
        }

        NrResult<NrPayloadRef> payloadResult = psnr::core::NrPayloadRefFactory::CreateFramedPayloadRef(
            *impl_->memoryPoolManager, packetType, std::span<const std::byte>(payload.data, payload.size));
        if (payloadResult.Failed())
        {
            return payloadResult.Status();
        }

        NrPayloadRef framedPayload = payloadResult.TakeValue();
        for (std::uint32_t index = 0; index < channels.size; ++index)
        {
            const NrSessionSendChannel& channel = channels.data[index];
            ++report.attempted;

            NrSessionSendChannelControl* control = internal::NrSessionSendChannelAccess::GetControl(channel);
            const NrStatus enqueueStatus = control == nullptr || !channel.IsOpen()
                                               ? NrStatus::Failure(NrErrorCode::InvalidState)
                                               : control->EnqueueSendRequested(framedPayload.Share(), submissionPermit);
            if (enqueueStatus.Succeeded())
            {
                ++report.accepted;
            }
            else
            {
                ++report.rejected;
            }
        }

        outReport = report;
        return NrStatus::Success();
    }
} // namespace psnr::runtime
