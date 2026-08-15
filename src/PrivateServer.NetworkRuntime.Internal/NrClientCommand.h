#pragma once

#include "NrEndpoint.h"

#include <cstdint>

namespace psnr::runtime::internal
{
    enum class NrClientCommandKind : std::uint8_t
    {
        None,
        Connect,
        Disconnect,
        Shutdown,
        EventSpaceAvailable,
    };

    struct NrClientCommand final
    {
        NrClientCommandKind kind = NrClientCommandKind::None;
        std::uint64_t attemptGeneration = 0;
        NrEndpoint remoteEndpoint;

        [[nodiscard]] bool IsValid() const noexcept
        {
            switch (kind)
            {
            case NrClientCommandKind::Connect:
                return attemptGeneration != 0 && remoteEndpoint.port != 0;

            case NrClientCommandKind::Disconnect:
                return attemptGeneration != 0;

            case NrClientCommandKind::Shutdown:
            case NrClientCommandKind::EventSpaceAvailable:
                return attemptGeneration == 0;

            case NrClientCommandKind::None:
            default:
                return false;
            }
        }
    };
} // namespace psnr::runtime::internal
