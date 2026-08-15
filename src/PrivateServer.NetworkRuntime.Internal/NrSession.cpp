#include "pch.h"

#include "NrSession.h"

#include "NrErrorCode.h"

#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    NrSession::NrSession(NrSession&& other) noexcept
        : sessionKey_(std::exchange(other.sessionKey_, 0))
        , socket_(std::move(other.socket_))
        , recvBuffer_(std::move(other.recvBuffer_))
        , pendingRecv_(std::move(other.pendingRecv_))
        , pendingSend_(std::move(other.pendingSend_))
    {
    }

    NrSession& NrSession::operator=(NrSession&& other) noexcept
    {
        if (this != &other)
        {
            sessionKey_ = std::exchange(other.sessionKey_, 0);
            socket_ = std::move(other.socket_);
            recvBuffer_ = std::move(other.recvBuffer_);
            pendingRecv_ = std::move(other.pendingRecv_);
            pendingSend_ = std::move(other.pendingSend_);
        }

        return *this;
    }

    NrResult<NrSession> NrSession::Create(NrSessionKey sessionKey, NrWin32Socket&& acceptedSocket,
                                          NrMemoryPoolManager& memoryPoolManager,
                                          std::size_t recvBufferCapacity) noexcept
    {
        if (sessionKey == 0 || !acceptedSocket.IsValid() ||
            acceptedSocket.State() != NrWin32SocketState::ConnectedTcpIPv4)
        {
            return NrResult<NrSession>::Failure(NrErrorCode::InvalidArgument);
        }

        NrResult<NrRecvBuffer> recvBufferResult = NrRecvBuffer::Create(memoryPoolManager, recvBufferCapacity);
        if (recvBufferResult.Failed())
        {
            return NrResult<NrSession>::Failure(recvBufferResult.Status());
        }

        return NrResult<NrSession>(NrSession(sessionKey, std::move(acceptedSocket), recvBufferResult.TakeValue()));
    }

    bool NrSession::IsValid() const noexcept
    {
        return sessionKey_ != 0 && socket_.IsValid() && recvBuffer_.Capacity() > 0;
    }

    NrSessionKey NrSession::SessionKey() const noexcept
    {
        return sessionKey_;
    }

    NrWin32Socket& NrSession::Socket() noexcept
    {
        return socket_;
    }

    const NrWin32Socket& NrSession::Socket() const noexcept
    {
        return socket_;
    }

    NrRecvBuffer& NrSession::RecvBuffer() noexcept
    {
        return recvBuffer_;
    }

    const NrRecvBuffer& NrSession::RecvBuffer() const noexcept
    {
        return recvBuffer_;
    }

    bool NrSession::HasPendingRecv() const noexcept
    {
        return pendingRecv_.IsValid();
    }

    NrRecvIoContextLease& NrSession::PendingRecv() noexcept
    {
        return pendingRecv_;
    }

    const NrRecvIoContextLease& NrSession::PendingRecv() const noexcept
    {
        return pendingRecv_;
    }

    NrStatus NrSession::SetPendingRecv(NrRecvIoContextLease&& pendingRecv) noexcept
    {
        if (pendingRecv_.IsValid() || !pendingRecv.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        pendingRecv_ = std::move(pendingRecv);
        return NrStatus::Success();
    }

    void NrSession::ClearPendingRecv() noexcept
    {
        pendingRecv_.Reset();
    }

    bool NrSession::HasPendingSend() const noexcept
    {
        return pendingSend_.IsValid();
    }

    NrSendIoContextLease& NrSession::PendingSend() noexcept
    {
        return pendingSend_;
    }

    const NrSendIoContextLease& NrSession::PendingSend() const noexcept
    {
        return pendingSend_;
    }

    NrStatus NrSession::SetPendingSend(NrSendIoContextLease&& pendingSend) noexcept
    {
        if (pendingSend_.IsValid() || !pendingSend.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        pendingSend_ = std::move(pendingSend);
        return NrStatus::Success();
    }

    void NrSession::ClearPendingSend() noexcept
    {
        pendingSend_.Reset();
    }

    NrSession::NrSession(NrSessionKey sessionKey, NrWin32Socket&& acceptedSocket, NrRecvBuffer&& recvBuffer) noexcept
        : sessionKey_(sessionKey)
        , socket_(std::move(acceptedSocket))
        , recvBuffer_(std::move(recvBuffer))
    {
    }
} // namespace psnr::runtime
