#pragma once

#include <cstdint>

namespace psnr::runtime
{
    struct NrUtf8View final
    {
        const char* data = nullptr;
        std::uint32_t size = 0;
    };
} // namespace psnr::runtime
