#include "pch.h"

#include "NrAcceptIoContext.h"

#include <utility>

namespace psnr::runtime
{
    NrAcceptIoContext::NrAcceptIoContext() noexcept
    {
        ResetForPost();
    }

    NrAcceptIoContext* NrAcceptIoContext::FromOverlapped(OVERLAPPED* overlapped) noexcept
    {
        if (NrIocpIoContextHeader::FromOverlapped(overlapped) == nullptr)
        {
            return nullptr;
        }

        return reinterpret_cast<NrAcceptIoContext*>(overlapped);
    }

    OVERLAPPED* NrAcceptIoContext::Overlapped() noexcept
    {
        return header_.Overlapped();
    }

    NrIoOperationType NrAcceptIoContext::Type() const noexcept
    {
        return header_.Type();
    }

    NrWin32Socket& NrAcceptIoContext::AcceptedSocket() noexcept
    {
        return acceptedSocket_;
    }

    NrWin32Socket NrAcceptIoContext::TakeAcceptedSocket() noexcept
    {
        return std::move(acceptedSocket_);
    }

    void* NrAcceptIoContext::Buffer() noexcept
    {
        return buffer_.data();
    }

    DWORD NrAcceptIoContext::BufferLength() const noexcept
    {
        return static_cast<DWORD>(buffer_.size());
    }

    DWORD NrAcceptIoContext::InitialReceiveLength() const noexcept
    {
        return static_cast<DWORD>(NrAcceptInitialReceiveLength);
    }

    DWORD NrAcceptIoContext::LocalAddressLength() const noexcept
    {
        return static_cast<DWORD>(NrAcceptAddressLength);
    }

    DWORD NrAcceptIoContext::RemoteAddressLength() const noexcept
    {
        return static_cast<DWORD>(NrAcceptAddressLength);
    }

    void NrAcceptIoContext::ResetForPost() noexcept
    {
        header_.Reset(NrIoOperationType::Accept);
        buffer_.fill(0);
    }
} // namespace psnr::runtime
