#pragma once

namespace psnr::core
{
    enum class NrProtocolErrorReason
    {
        None,
        InvalidLength,
        PacketTooLarge,
        UnsupportedVersion,
        ReservedFlags,
    };

} // namespace psnr::core
