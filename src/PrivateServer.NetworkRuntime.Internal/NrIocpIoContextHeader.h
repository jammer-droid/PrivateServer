#pragma once

#include "NrIoOperationType.h"
#include "NrWindows.h"

#include <cstddef>

namespace psnr::runtime
{
    struct NrIocpIoContextHeader final
    {
        [[nodiscard]] static NrIocpIoContextHeader* FromOverlapped(OVERLAPPED* ownerOverlapped) noexcept
        {
            if (ownerOverlapped == nullptr)
            {
                return nullptr;
            }

            return reinterpret_cast<NrIocpIoContextHeader*>(ownerOverlapped);
        }

        [[nodiscard]] OVERLAPPED* Overlapped() noexcept
        {
            return &overlapped;
        }

        [[nodiscard]] NrIoOperationType Type() const noexcept
        {
            return type;
        }

        void Reset(NrIoOperationType operationType) noexcept
        {
            overlapped = OVERLAPPED{};
            type = operationType;
        }

        // Must stay first. IOCP returns this OVERLAPPED* address.
        OVERLAPPED overlapped{};
        NrIoOperationType type = NrIoOperationType::Unknown;
    };

    static_assert(offsetof(NrIocpIoContextHeader, overlapped) == 0);
} // namespace psnr::runtime
