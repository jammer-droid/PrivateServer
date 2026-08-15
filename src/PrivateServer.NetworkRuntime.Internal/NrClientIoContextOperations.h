#pragma once

#include "NrPayloadRef.h"
#include "NrRecvIoContext.h"
#include "NrSendIoContext.h"
#include "NrStatus.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::runtime::internal
{
    enum class NrClientSendCompletionResult
    {
        None = 0,
        NeedsRepost,
        Completed,
    };

    class NrClientIoContextOperations final
    {
    public:
        NrClientIoContextOperations() noexcept = default;

        NrClientIoContextOperations(const NrClientIoContextOperations&) = delete;
        NrClientIoContextOperations& operator=(const NrClientIoContextOperations&) = delete;

        NrClientIoContextOperations(NrClientIoContextOperations&&) = delete;
        NrClientIoContextOperations& operator=(NrClientIoContextOperations&&) = delete;

        [[nodiscard]] psnr::core::NrStatus PrepareRecv(NrRecvIoContext& context, std::uint64_t attemptGeneration,
                                                       std::span<std::byte> writableBuffer) noexcept;
        // Native I/O를 취소하지 않는다. completion을 dequeue했거나 native post가 거절된 뒤 pending gate만 해제한다.
        [[nodiscard]] psnr::core::NrStatus ReleasePendingRecv(NrRecvIoContext& context) noexcept;

        [[nodiscard]] psnr::core::NrStatus PrepareSendContext(NrSendIoContext& context, std::uint64_t attemptGeneration,
                                                              psnr::core::NrPayloadRef&& payload) noexcept;
        [[nodiscard]] psnr::core::NrStatus PrepareSendContextForRepost(NrSendIoContext& context) noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteSendContext(NrSendIoContext& context,
                                                               psnr::core::NrStatus completionStatus,
                                                               std::uint32_t bytesTransferred,
                                                               NrClientSendCompletionResult& result) noexcept;
        // Native I/O를 취소하지 않는다. completion을 dequeue했거나 native post가 거절된 뒤 pending gate만 해제한다.
        [[nodiscard]] psnr::core::NrStatus ReleasePendingSend(NrSendIoContext& context) noexcept;

        [[nodiscard]] bool HasPendingRecv() const noexcept;
        [[nodiscard]] bool HasPendingSend() const noexcept;

        [[nodiscard]] static std::uint64_t AttemptGeneration(const NrRecvIoContext& context) noexcept;
        [[nodiscard]] static std::uint64_t AttemptGeneration(const NrSendIoContext& context) noexcept;

    private:
        NrRecvIoContext* pendingRecv_ = nullptr; // waiting for WSARecv completion
        NrSendIoContext* pendingSend_ = nullptr; // waiting for WSASend completion
    };
} // namespace psnr::runtime::internal
