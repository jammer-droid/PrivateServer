#pragma once

#include "NrClientControlCompletion.h"
#include "NrIocpCompletionHandler.h"
#include "NrIoOperationType.h"
#include "NrStatus.h"

#include <cstdint>

namespace psnr::runtime
{
    struct NrRecvIoContext;
    struct NrSendIoContext;
} // namespace psnr::runtime

namespace psnr::runtime::internal
{
    class NrClientConnectIoContext;

    class INrClientIoCompletionTarget
    {
    public:
        INrClientIoCompletionTarget() noexcept = default;

        INrClientIoCompletionTarget(const INrClientIoCompletionTarget&) = delete;
        INrClientIoCompletionTarget& operator=(const INrClientIoCompletionTarget&) = delete;

        virtual ~INrClientIoCompletionTarget() noexcept = default;

        [[nodiscard]] virtual std::uint64_t CurrentAttemptGeneration() const noexcept = 0;

        [[nodiscard]] virtual psnr::core::NrStatus HandleClientControlCompletion(
            NrClientControlCompletionKind kind) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus HandleConnectCompletion(
            NrClientConnectIoContext& context, const NrIocpCompletionPacket& packet) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus HandleRecvCompletion(
            NrRecvIoContext& context, const NrIocpCompletionPacket& packet) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus HandleSendCompletion(
            NrSendIoContext& context, const NrIocpCompletionPacket& packet) noexcept = 0;
        [[nodiscard]] virtual psnr::core::NrStatus HandleStaleIoCompletion(
            NrIoOperationType operationType, std::uint64_t attemptGeneration,
            const NrIocpCompletionPacket& packet) noexcept = 0;
    };

    class NrClientIoCompletionDispatcher final : public INrIocpCompletionHandler
    {
    public:
        explicit NrClientIoCompletionDispatcher(INrClientIoCompletionTarget& target) noexcept;

        [[nodiscard]] psnr::core::NrStatus HandleIoCompletion(const NrIocpCompletionPacket& packet) noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleControlCompletion(
            const NrIocpCompletionPacket& packet) noexcept override;

    private:
        [[nodiscard]] psnr::core::NrStatus RouteByGeneration(NrIoOperationType operationType,
                                                             std::uint64_t attemptGeneration,
                                                             const NrIocpCompletionPacket& packet) noexcept;

        INrClientIoCompletionTarget& target_;
    };
} // namespace psnr::runtime::internal
