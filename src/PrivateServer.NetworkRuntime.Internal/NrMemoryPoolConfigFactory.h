#pragma once

#include "NrMemoryPoolManager.h"

#include <cstddef>

namespace psnr::core
{
    class NrMemoryPoolConfigFactory final
    {
    public:
        NrMemoryPoolConfigFactory() = delete;

        [[nodiscard]] static NrMemoryPoolManagerConfig CreateTinyTestConfig() noexcept;
    };

} // namespace psnr::core
