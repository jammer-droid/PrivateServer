#include "pch.h"

#include "NrClientConnectIoContext.h"

namespace psnr::runtime::internal
{
    NrClientConnectIoContext::NrClientConnectIoContext(const std::uint64_t attemptGeneration,
                                                       const NrSocketAddressWin32& remoteAddress) noexcept
        : attemptGeneration_(attemptGeneration)
        , remoteAddress_(remoteAddress)
    {
        header_.Reset(NrIoOperationType::Connect);
    }

    NrClientConnectIoContext* NrClientConnectIoContext::FromOverlapped(OVERLAPPED* overlapped) noexcept
    {
        if (NrIocpIoContextHeader::FromOverlapped(overlapped) == nullptr)
        {
            return nullptr;
        }

        return reinterpret_cast<NrClientConnectIoContext*>(overlapped);
    }

    OVERLAPPED* NrClientConnectIoContext::Overlapped() noexcept
    {
        return header_.Overlapped();
    }

    NrIoOperationType NrClientConnectIoContext::Type() const noexcept
    {
        return header_.Type();
    }

    std::uint64_t NrClientConnectIoContext::AttemptGeneration() const noexcept
    {
        return attemptGeneration_;
    }

    const sockaddr* NrClientConnectIoContext::RemoteAddress() const noexcept
    {
        return reinterpret_cast<const sockaddr*>(&remoteAddress_.address);
    }

    int NrClientConnectIoContext::RemoteAddressLength() const noexcept
    {
        return remoteAddress_.length;
    }
} // namespace psnr::runtime::internal
