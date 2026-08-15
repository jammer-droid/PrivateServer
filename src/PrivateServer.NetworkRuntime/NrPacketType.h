#pragma once

#include <cstdint>

namespace psnr::core
{
    struct NrPacketType final
    {
        std::uint16_t value = 0;

        [[nodiscard]] friend constexpr bool operator==(NrPacketType, NrPacketType) noexcept = default;
    };

} // namespace psnr::core
