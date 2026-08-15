#pragma once

#include <cstdint>
#include <string_view>

namespace psnr::logging
{
    struct ApplicationBuildInfo final
    {
        std::string_view configuration;
        std::string_view architecture;
        std::uint32_t msvcVersion = 0;
        std::uint32_t msvcFullVersion = 0;

        [[nodiscard]] static ApplicationBuildInfo Current() noexcept;
    };
} // namespace psnr::logging
