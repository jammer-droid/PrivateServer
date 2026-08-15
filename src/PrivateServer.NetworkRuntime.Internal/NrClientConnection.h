#pragma once

#include "NrClientConnectIoContext.h"
#include "NrClientIoContextOperations.h"
#include "NrPacketParser.h"
#include "NrRecvBuffer.h"
#include "NrRecvIoContext.h"
#include "NrResult.h"
#include "NrSendIoContext.h"
#include "NrStatus.h"
#include "NrWin32Socket.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace psnr::runtime
{
    class NrIocpPort;
    struct NrIocpCompletionPacket;
} // namespace psnr::runtime

namespace psnr::core
{
    class NrMemoryPoolManager;
}

namespace psnr::runtime::internal
{
    class NrClientConnection final
    {
    public:
        NrClientConnection(const NrClientConnection&) = delete;
        NrClientConnection& operator=(const NrClientConnection&) = delete;

        NrClientConnection(NrClientConnection&&) = delete;
        NrClientConnection& operator=(NrClientConnection&&) = delete;

        ~NrClientConnection() noexcept = default;

        [[nodiscard]] static psnr::core::NrResult<std::unique_ptr<NrClientConnection>> Create(
            std::uint64_t attemptGeneration, const NrSocketAddressWin32& remoteAddress,
            psnr::core::NrMemoryPoolManager& memoryPoolManager) noexcept;

        [[nodiscard]] std::uint64_t AttemptGeneration() const noexcept;
        [[nodiscard]] NrWin32SocketState SocketState() const noexcept;
        [[nodiscard]] NrClientConnectIoContext& ConnectContext() noexcept;
        [[nodiscard]] NrRecvIoContext& RecvContext() noexcept;
        [[nodiscard]] NrSendIoContext& SendContext() noexcept;
        [[nodiscard]] bool HasPendingRecv() const noexcept;
        [[nodiscard]] bool HasPendingSend() const noexcept;
        [[nodiscard]] std::size_t ReadableRecvBytes() const noexcept;

        [[nodiscard]] psnr::core::NrStatus StartConnect(NrIocpPort& iocpPort) noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteConnect() noexcept;
        [[nodiscard]] psnr::core::NrStatus PostRecv() noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteRecv(NrRecvIoContext& context,
                                                        const NrIocpCompletionPacket& packet) noexcept;
        [[nodiscard]] psnr::core::NrStatus PostSend(psnr::core::NrPayloadRef&& payload) noexcept;
        [[nodiscard]] psnr::core::NrStatus CompleteSend(NrSendIoContext& context, const NrIocpCompletionPacket& packet,
                                                        NrClientSendCompletionResult& result) noexcept;
        [[nodiscard]] psnr::core::NrStatus RepostSend() noexcept;
        [[nodiscard]] psnr::core::NrStatus ParseNextReceivedPacket(
            psnr::core::NrPacketParseResult& result) const noexcept;
        [[nodiscard]] psnr::core::NrStatus ConsumeReceivedPacket(std::size_t packetLength) noexcept;
        [[nodiscard]] psnr::core::NrStatus Close() noexcept;

    private:
        NrClientConnection(std::uint64_t attemptGeneration, const NrSocketAddressWin32& remoteAddress,
                           psnr::core::NrRecvBuffer&& recvBuffer, psnr::core::NrPacketParser&& packetParser,
                           NrRecvIoContextLease&& recvContext, NrSendIoContextLease&& sendContext) noexcept;

        NrClientConnectIoContext connectContext_;
        psnr::core::NrRecvBuffer recvBuffer_;
        psnr::core::NrPacketParser packetParser_;
        NrRecvIoContextLease recvContext_;
        NrSendIoContextLease sendContext_;
        NrClientIoContextOperations ioContextOperations_;
        NrWin32Socket socket_;
    };
} // namespace psnr::runtime::internal
