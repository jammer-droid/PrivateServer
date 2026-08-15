#include "pch.h"

#include "NrIocpPort.h"

#include "NrErrorCode.h"
#include "NrWin32Socket.h"

#include <utility>

namespace psnr::runtime
{
    using psnr::core::NrErrorCode;

    namespace
    {
        [[nodiscard]] NrStatus LastWin32ErrorStatus() noexcept
        {
            return NrStatus::Failure(NrErrorCode::IoFailed, static_cast<psnr::core::NrNativeErrorCode>(GetLastError()));
        }

        [[nodiscard]] NrStatus CompletionIoStatus(DWORD errorCode) noexcept
        {
            if (errorCode == ERROR_OPERATION_ABORTED)
            {
                return NrStatus::Failure(NrErrorCode::OperationCanceled,
                                         static_cast<psnr::core::NrNativeErrorCode>(errorCode));
            }

            return NrStatus::Failure(NrErrorCode::IoFailed, static_cast<psnr::core::NrNativeErrorCode>(errorCode));
        }
    } // namespace

    NrIocpPort::~NrIocpPort() noexcept
    {
        (void)Close();
    }

    NrIocpPort::NrIocpPort(NrIocpPort&& other) noexcept
        : port_(std::exchange(other.port_, nullptr))
    {
    }

    NrIocpPort& NrIocpPort::operator=(NrIocpPort&& other) noexcept
    {
        if (this != &other)
        {
            (void)Close();
            port_ = std::exchange(other.port_, nullptr);
        }

        return *this;
    }

    bool NrIocpPort::IsValid() const noexcept
    {
        return port_ != nullptr;
    }

    NrStatus NrIocpPort::Create() noexcept
    {
        if (IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (port == nullptr)
        {
            return LastWin32ErrorStatus();
        }

        port_ = port;
        return NrStatus::Success();
    }

    NrStatus NrIocpPort::AssociateSocket(const NrWin32Socket& socket, std::uintptr_t completionKey) noexcept
    {
        if (!IsValid() || !socket.IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        HANDLE result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket.NativeSocket()), port_,
                                               static_cast<ULONG_PTR>(completionKey), 0);
        if (result == nullptr)
        {
            return LastWin32ErrorStatus();
        }

        if (result != port_)
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        return NrStatus::Success();
    }

    NrStatus NrIocpPort::WaitForCompletion(NrIocpCompletionPacket& outPacket) noexcept
    {
        if (!IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        const BOOL result = GetQueuedCompletionStatus(port_, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        outPacket.bytesTransferred = bytesTransferred;
        outPacket.completionKey = static_cast<std::uintptr_t>(completionKey);
        outPacket.overlapped = overlapped;
        outPacket.ioStatus = NrStatus::Success();

        if (result == FALSE)
        {
            const DWORD errorCode = GetLastError();
            if (overlapped == nullptr) // completion dequeue failed
            {
                return CompletionIoStatus(errorCode);
            }

            // IO failed
            outPacket.ioStatus = CompletionIoStatus(errorCode);
        }

        return NrStatus::Success();
    }

    NrStatus NrIocpPort::PostControlCompletion(std::uintptr_t completionKey) noexcept
    {
        if (!IsValid())
        {
            return NrStatus::Failure(NrErrorCode::InvalidState);
        }

        const BOOL result = PostQueuedCompletionStatus(port_, 0, static_cast<ULONG_PTR>(completionKey), nullptr);
        if (result == FALSE)
        {
            return LastWin32ErrorStatus();
        }

        return NrStatus::Success();
    }

    NrStatus NrIocpPort::Close() noexcept
    {
        if (!IsValid())
        {
            return NrStatus::Success();
        }

        HANDLE port = port_;
        port_ = nullptr;

        if (!CloseHandle(port))
        {
            return LastWin32ErrorStatus();
        }

        return NrStatus::Success();
    }
} // namespace psnr::runtime
