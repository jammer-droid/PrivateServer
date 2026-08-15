#pragma once

#include "NrActor.h"
#include "NrClientCommandChannel.h"
#include "NrClientIoCompletionDispatcher.h"
#include "NrClientLifecycleStateMachine.h"
#include "NrStatus.h"
#include "NrWin32Socket.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace psnr::runtime
{
    struct NrEndpoint;
    class NrIocpPort;
} // namespace psnr::runtime

namespace psnr::core
{
    class NrMemoryPoolManager;
} // namespace psnr::core

namespace psnr::runtime::internal
{
    class INrClientTransportEventSink;
    class NrClientConnection;

    class NrClientTransport final : public psnr::core::INrActor, public INrClientIoCompletionTarget
    {
    public:
        NrClientTransport(NrIocpPort& iocpPort, psnr::core::NrMemoryPoolManager& memoryPoolManager,
                          NrClientCommandChannel& commandChannel, NrClientLifecycleStateMachine& lifecycleStateMachine,
                          INrClientTransportEventSink& eventSink) noexcept;

        NrClientTransport(const NrClientTransport&) = delete;
        NrClientTransport& operator=(const NrClientTransport&) = delete;

        NrClientTransport(NrClientTransport&&) = delete;
        NrClientTransport& operator=(NrClientTransport&&) = delete;

        ~NrClientTransport() noexcept;

        [[nodiscard]] bool HasActiveConnection() const noexcept;
        [[nodiscard]] bool HasPendingRecv() const noexcept;
        [[nodiscard]] bool HasPendingSend() const noexcept;
        [[nodiscard]] std::uint64_t PendingConnectIoCount() const noexcept;
        [[nodiscard]] std::uint64_t PendingRecvIoCount() const noexcept;
        [[nodiscard]] std::uint64_t PendingSendIoCount() const noexcept;
        [[nodiscard]] std::size_t PendingSendQueueDepth() const noexcept;
        [[nodiscard]] std::size_t PendingSendQueueHighWatermark() const noexcept;
        [[nodiscard]] NrWin32SocketState ConnectionSocketState() const noexcept;

        [[nodiscard]] psnr::core::NrResult<psnr::core::NrActorDrainReport> Drain(
            psnr::core::NrActorDrainBudget budget) noexcept override;

        [[nodiscard]] std::uint64_t CurrentAttemptGeneration() const noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleClientControlCompletion(
            NrClientControlCompletionKind kind) noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleConnectCompletion(
            NrClientConnectIoContext& context, const NrIocpCompletionPacket& packet) noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleRecvCompletion(NrRecvIoContext& context,
                                                                const NrIocpCompletionPacket& packet) noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleSendCompletion(NrSendIoContext& context,
                                                                const NrIocpCompletionPacket& packet) noexcept override;
        [[nodiscard]] psnr::core::NrStatus HandleStaleIoCompletion(
            NrIoOperationType operationType, std::uint64_t attemptGeneration,
            const NrIocpCompletionPacket& packet) noexcept override;

    private:
        [[nodiscard]] psnr::core::NrStatus HandleCommand(const NrClientCommand& command) noexcept;
        [[nodiscard]] psnr::core::NrStatus StartConnect(std::uint64_t attemptGeneration,
                                                        const NrEndpoint& remoteEndpoint) noexcept;
        [[nodiscard]] psnr::core::NrStatus RequestDisconnect() noexcept;
        [[nodiscard]] psnr::core::NrStatus RequestShutdown() noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteShutdown() noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordConnectSuccess() noexcept;
        [[nodiscard]] psnr::core::NrStatus RecordConnectFailure(psnr::core::NrStatus transportStatus) noexcept;
        [[nodiscard]] psnr::core::NrStatus TryStartNextSend() noexcept;
        [[nodiscard]] psnr::core::NrStatus TryConsumeNextSend(bool& outConsumed) noexcept;
        [[nodiscard]] psnr::core::NrStatus DiscardQueuedSends() noexcept;
        void ReleaseActiveSendAdmission() noexcept;
        [[nodiscard]] bool IsDisconnectInProgress() const noexcept;
        [[nodiscard]] psnr::core::NrStatus BeginDisconnect(NrClientDisconnectReason reason,
                                                           psnr::core::NrStatus transportStatus) noexcept;
        [[nodiscard]] psnr::core::NrStatus TryFinishDisconnect() noexcept;

        NrIocpPort& iocpPort_;
        psnr::core::NrMemoryPoolManager& memoryPoolManager_;
        NrClientCommandChannel& commandChannel_;
        NrClientLifecycleStateMachine& lifecycleStateMachine_;
        INrClientTransportEventSink& eventSink_;
        std::unique_ptr<NrClientConnection> connection_;

        bool activeSendUsesAdmission_ = false;
        bool shutdownRequested_ = false;
        bool workerStopPosted_ = false;

        std::atomic<std::uint64_t> pendingConnectIoCount_{0};
        std::atomic<std::uint64_t> pendingRecvIoCount_{0};
        std::atomic<std::uint64_t> pendingSendIoCount_{0};

        NrClientDisconnectReason pendingDisconnectReason_ = NrClientDisconnectReason::None;
        psnr::core::NrStatus pendingDisconnectStatus_;
    };
} // namespace psnr::runtime::internal
