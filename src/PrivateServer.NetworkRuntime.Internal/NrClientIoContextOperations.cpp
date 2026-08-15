#include "pch.h"

#include "NrClientIoContextOperations.h"

#include "NrErrorCode.h"
#include "NrIoOperationType.h"

#include <limits>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrStatus;

    namespace
    {
        void ResetSendContextToIdle(NrSendIoContext& context) noexcept
        {
            context.payloadRef = psnr::core::NrPayloadRef{};
            context.payloadLength = 0;
            context.bytesSent = 0;
            context.RefreshWsaBuffer();
        }
    } // namespace

    NrStatus NrClientIoContextOperations::PrepareRecv(NrRecvIoContext& context, const std::uint64_t attemptGeneration,
                                                      const std::span<std::byte> writableBuffer) noexcept
    {
        if (attemptGeneration == 0 || writableBuffer.empty() ||
            writableBuffer.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (pendingRecv_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        context.header.Reset(NrIoOperationType::Recv);
        // 공용 context의 sessionKey 저장 공간을 클라이언트에서는 연결 시도 generation으로 사용한다.
        // 이 값은 서버가 발급하는 Runtime Session Key와 무관한 NrClient 로컬 증가 값이다.
        context.sessionKey = attemptGeneration;
        context.writableLength = static_cast<std::uint32_t>(writableBuffer.size());
        context.wsabuf.buf = reinterpret_cast<char*>(writableBuffer.data());
        context.wsabuf.len = context.writableLength;
        pendingRecv_ = &context;
        return NrStatus::Success();
    }

    NrStatus NrClientIoContextOperations::ReleasePendingRecv(NrRecvIoContext& context) noexcept
    {
        if (pendingRecv_ != &context)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        pendingRecv_ = nullptr;
        return NrStatus::Success();
    }

    NrStatus NrClientIoContextOperations::PrepareSendContext(NrSendIoContext& context,
                                                             const std::uint64_t attemptGeneration,
                                                             psnr::core::NrPayloadRef&& payload) noexcept
    {
        if (attemptGeneration == 0 || payload.IsEmpty())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (pendingSend_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (!context.payloadRef.IsEmpty() && !context.IsFullySent())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        context.header.Reset(NrIoOperationType::Send);
        // 공용 context의 sessionKey 저장 공간을 클라이언트에서는 연결 시도 generation으로 사용한다.
        // 이 값은 서버가 발급하는 Runtime Session Key와 무관한 NrClient 로컬 증가 값이다.
        context.sessionKey = attemptGeneration;
        context.payloadRef = std::move(payload);
        context.payloadLength = static_cast<std::uint32_t>(context.payloadRef.Length());
        context.bytesSent = 0;
        context.RefreshWsaBuffer();
        pendingSend_ = &context;
        return NrStatus::Success();
    }

    NrStatus NrClientIoContextOperations::PrepareSendContextForRepost(NrSendIoContext& context) noexcept
    {
        if (pendingSend_ != nullptr)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (context.payloadRef.IsEmpty() || context.IsFullySent() || context.wsabuf.buf == nullptr ||
            context.wsabuf.len == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        context.header.Reset(NrIoOperationType::Send);
        pendingSend_ = &context;
        return NrStatus::Success();
    }

    NrStatus NrClientIoContextOperations::CompleteSendContext(NrSendIoContext& context, const NrStatus completionStatus,
                                                              const std::uint32_t bytesTransferred,
                                                              NrClientSendCompletionResult& result) noexcept
    {
        result = NrClientSendCompletionResult::None;

        const NrStatus releaseStatus = ReleasePendingSend(context);
        if (releaseStatus.Failed())
        {
            return releaseStatus;
        }

        if (completionStatus.Failed())
        {
            ResetSendContextToIdle(context);
            return completionStatus;
        }

        if (bytesTransferred == 0)
        {
            ResetSendContextToIdle(context);
            return NrStatus::Failure(NrErrorCode::IoFailed);
        }

        const NrStatus advanceStatus = context.AdvanceBytesSent(bytesTransferred);
        if (advanceStatus.Failed())
        {
            ResetSendContextToIdle(context);
            return advanceStatus;
        }

        if (context.IsFullySent())
        {
            ResetSendContextToIdle(context);
            result = NrClientSendCompletionResult::Completed;
            return NrStatus::Success();
        }

        result = NrClientSendCompletionResult::NeedsRepost;
        return NrStatus::Success();
    }

    NrStatus NrClientIoContextOperations::ReleasePendingSend(NrSendIoContext& context) noexcept
    {
        if (pendingSend_ != &context)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        pendingSend_ = nullptr;
        return NrStatus::Success();
    }

    bool NrClientIoContextOperations::HasPendingRecv() const noexcept
    {
        return pendingRecv_ != nullptr;
    }

    bool NrClientIoContextOperations::HasPendingSend() const noexcept
    {
        return pendingSend_ != nullptr;
    }

    std::uint64_t NrClientIoContextOperations::AttemptGeneration(const NrRecvIoContext& context) noexcept
    {
        // 클라이언트 의미로 해석하며 server session routing에 사용하지 않는다.
        return context.sessionKey;
    }

    std::uint64_t NrClientIoContextOperations::AttemptGeneration(const NrSendIoContext& context) noexcept
    {
        // 클라이언트 의미로 해석하며 server session routing에 사용하지 않는다.
        return context.sessionKey;
    }
} // namespace psnr::runtime::internal
