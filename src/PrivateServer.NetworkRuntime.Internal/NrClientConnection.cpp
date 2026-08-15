#include "pch.h"

#include "NrClientConnection.h"

#include "NrEndpoint.h"
#include "NrErrorCode.h"
#include "NrIocpCompletionPacket.h"
#include "NrIocpOverlappedContextFactory.h"
#include "NrIocpPort.h"
#include "NrMemoryPoolManager.h"
#include "NrPacketHeader.h"
#include "NrResult.h"

#include <new>
#include <span>
#include <utility>

namespace psnr::runtime::internal
{
    using psnr::core::NrErrorCode;
    using psnr::core::NrResult;
    using psnr::core::NrStatus;

    NrClientConnection::NrClientConnection(const std::uint64_t attemptGeneration,
                                           const NrSocketAddressWin32& remoteAddress,
                                           psnr::core::NrRecvBuffer&& recvBuffer,
                                           psnr::core::NrPacketParser&& packetParser,
                                           NrRecvIoContextLease&& recvContext,
                                           NrSendIoContextLease&& sendContext) noexcept
        : connectContext_(attemptGeneration, remoteAddress)
        , recvBuffer_(std::move(recvBuffer))
        , packetParser_(std::move(packetParser))
        , recvContext_(std::move(recvContext))
        , sendContext_(std::move(sendContext))
    {
    }

    NrResult<std::unique_ptr<NrClientConnection>> NrClientConnection::Create(
        const std::uint64_t attemptGeneration, const NrSocketAddressWin32& remoteAddress,
        psnr::core::NrMemoryPoolManager& memoryPoolManager) noexcept
    {
        if (attemptGeneration == 0)
        {
            return NrResult<std::unique_ptr<NrClientConnection>>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<psnr::core::NrRecvBuffer> recvBufferResult =
            psnr::core::NrRecvBuffer::Create(memoryPoolManager, psnr::core::NrMaxPacketLength);
        if (recvBufferResult.Failed())
        {
            return NrResult<std::unique_ptr<NrClientConnection>>::Failure(recvBufferResult.Status());
        }

        psnr::core::NrRecvBuffer recvBuffer = recvBufferResult.TakeValue();
        NrResult<psnr::core::NrPacketParser> packetParserResult =
            psnr::core::NrPacketParser::Create(psnr::core::NrPacketParserConfig{});
        if (packetParserResult.Failed())
        {
            return NrResult<std::unique_ptr<NrClientConnection>>::Failure(packetParserResult.Status());
        }

        NrIocpOverlappedContextFactory contextFactory(memoryPoolManager);
        NrResult<NrRecvIoContextLease> recvContextResult =
            contextFactory.CreateRecv(attemptGeneration, recvBuffer.WritableSpan());
        if (recvContextResult.Failed())
        {
            return NrResult<std::unique_ptr<NrClientConnection>>::Failure(recvContextResult.Status());
        }

        NrResult<NrSendIoContextLease> sendContextResult = contextFactory.CreateSendContext(attemptGeneration);
        if (sendContextResult.Failed())
        {
            return NrResult<std::unique_ptr<NrClientConnection>>::Failure(sendContextResult.Status());
        }

        std::unique_ptr<NrClientConnection> connection(new (std::nothrow) NrClientConnection(
            attemptGeneration, remoteAddress, std::move(recvBuffer), packetParserResult.TakeValue(),
            recvContextResult.TakeValue(), sendContextResult.TakeValue()));
        if (connection == nullptr)
        {
            return NrResult<std::unique_ptr<NrClientConnection>>::Failure(NrErrorCode::OutOfMemory);
        }

        return NrResult<std::unique_ptr<NrClientConnection>>(std::move(connection));
    }

    std::uint64_t NrClientConnection::AttemptGeneration() const noexcept
    {
        return connectContext_.AttemptGeneration();
    }

    NrWin32SocketState NrClientConnection::SocketState() const noexcept
    {
        return socket_.State();
    }

    NrClientConnectIoContext& NrClientConnection::ConnectContext() noexcept
    {
        return connectContext_;
    }

    NrRecvIoContext& NrClientConnection::RecvContext() noexcept
    {
        return recvContext_.Context();
    }

    NrSendIoContext& NrClientConnection::SendContext() noexcept
    {
        return sendContext_.Context();
    }

    bool NrClientConnection::HasPendingRecv() const noexcept
    {
        return ioContextOperations_.HasPendingRecv();
    }

    bool NrClientConnection::HasPendingSend() const noexcept
    {
        return ioContextOperations_.HasPendingSend();
    }

    std::size_t NrClientConnection::ReadableRecvBytes() const noexcept
    {
        return recvBuffer_.ReadableBytes();
    }

    NrStatus NrClientConnection::StartConnect(NrIocpPort& iocpPort) noexcept
    {
        if (AttemptGeneration() == 0)
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (socket_.State() != NrWin32SocketState::Closed)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const NrStatus openStatus = socket_.OpenOverlappedTcpIPv4();
        if (openStatus.Failed())
        {
            return openStatus;
        }

        const NrEndpoint localEndpoint{NrEndpointAddressType::IPv4, NrIPv4Address::Any(), 0};
        const NrStatus bindStatus = socket_.Bind(localEndpoint);
        if (bindStatus.Failed())
        {
            (void)socket_.Close();
            return bindStatus;
        }

        const NrStatus associateStatus = iocpPort.AssociateSocket(socket_, 0);
        if (associateStatus.Failed())
        {
            (void)socket_.Close();
            return associateStatus;
        }

        const NrStatus loadStatus = socket_.LoadConnectEx();
        if (loadStatus.Failed())
        {
            (void)socket_.Close();
            return loadStatus;
        }

        const NrStatus postStatus = socket_.PostConnect(
            connectContext_.RemoteAddress(), connectContext_.RemoteAddressLength(), connectContext_.Overlapped());
        if (postStatus.Failed())
        {
            (void)socket_.Close();
            return postStatus;
        }

        return NrStatus::Success();
    }

    NrStatus NrClientConnection::CompleteConnect() noexcept
    {
        return socket_.UpdateConnectContext();
    }

    NrStatus NrClientConnection::PostRecv() noexcept
    {
        if (socket_.State() != NrWin32SocketState::ConnectedTcpIPv4)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        if (ioContextOperations_.HasPendingRecv())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        recvBuffer_.Compact();
        std::span<std::byte> writableBuffer = recvBuffer_.WritableSpan();
        if (writableBuffer.empty())
        {
            return NrStatus::Failure(NrErrorCode::CapacityExceeded);
        }

        NrRecvIoContext& context = recvContext_.Context();
        const NrStatus prepareStatus = ioContextOperations_.PrepareRecv(context, AttemptGeneration(), writableBuffer);
        if (prepareStatus.Failed())
        {
            return prepareStatus;
        }

        const NrStatus postStatus = socket_.PostRecv(context.wsabuf, recvContext_.Overlapped());
        if (postStatus.Failed())
        {
            (void)ioContextOperations_.ReleasePendingRecv(context);
            return postStatus;
        }

        return NrStatus::Success();
    }

    NrStatus NrClientConnection::CompleteRecv(NrRecvIoContext& context, const NrIocpCompletionPacket& packet) noexcept
    {
        if (!recvContext_.IsValid() || &context != &recvContext_.Context() ||
            packet.overlapped != recvContext_.Overlapped())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        if (!ioContextOperations_.HasPendingRecv())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrStatus completionStatus = packet.ioStatus;
        if (completionStatus.Succeeded())
        {
            completionStatus = recvBuffer_.CommitWritten(packet.bytesTransferred);
        }

        const NrStatus releaseStatus = ioContextOperations_.ReleasePendingRecv(context);
        return completionStatus.Failed() ? completionStatus : releaseStatus;
    }

    NrStatus NrClientConnection::PostSend(psnr::core::NrPayloadRef&& payload) noexcept
    {
        if (socket_.State() != NrWin32SocketState::ConnectedTcpIPv4 || ioContextOperations_.HasPendingSend())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrSendIoContext& context = sendContext_.Context();
        const NrStatus prepareStatus =
            ioContextOperations_.PrepareSendContext(context, AttemptGeneration(), std::move(payload));
        if (prepareStatus.Failed())
        {
            return prepareStatus;
        }

        const NrStatus postStatus = socket_.PostSend(context.wsabuf, sendContext_.Overlapped());
        if (postStatus.Failed())
        {
            (void)ioContextOperations_.ReleasePendingSend(context);
            return postStatus;
        }

        return NrStatus::Success();
    }

    NrStatus NrClientConnection::CompleteSend(NrSendIoContext& context, const NrIocpCompletionPacket& packet,
                                              NrClientSendCompletionResult& result) noexcept
    {
        result = NrClientSendCompletionResult::None;

        if (!sendContext_.IsValid() || &context != &sendContext_.Context() ||
            packet.overlapped != sendContext_.Overlapped())
        {
            return NrStatus::Failure(NrErrorCode::InvalidArgument);
        }

        return ioContextOperations_.CompleteSendContext(context, packet.ioStatus, packet.bytesTransferred, result);
    }

    NrStatus NrClientConnection::RepostSend() noexcept
    {
        if (socket_.State() != NrWin32SocketState::ConnectedTcpIPv4 || ioContextOperations_.HasPendingSend())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        NrSendIoContext& context = sendContext_.Context();
        const NrStatus prepareStatus = ioContextOperations_.PrepareSendContextForRepost(context);
        if (prepareStatus.Failed())
        {
            return prepareStatus;
        }

        const NrStatus postStatus = socket_.PostSend(context.wsabuf, sendContext_.Overlapped());
        if (postStatus.Failed())
        {
            (void)ioContextOperations_.ReleasePendingSend(context);
            return postStatus;
        }

        return NrStatus::Success();
    }

    NrStatus NrClientConnection::ParseNextReceivedPacket(psnr::core::NrPacketParseResult& result) const noexcept
    {
        return packetParser_.Parse(recvBuffer_.ReadableSpan(), result);
    }

    NrStatus NrClientConnection::ConsumeReceivedPacket(const std::size_t packetLength) noexcept
    {
        return recvBuffer_.Consume(packetLength);
    }

    NrStatus NrClientConnection::Close() noexcept
    {
        return socket_.Close();
    }
} // namespace psnr::runtime::internal
