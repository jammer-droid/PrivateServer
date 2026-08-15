#pragma once

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    struct NrByteView final
    {
        const std::byte* data = nullptr;
        std::uint32_t size = 0;
    };
} // namespace psnr::runtime
