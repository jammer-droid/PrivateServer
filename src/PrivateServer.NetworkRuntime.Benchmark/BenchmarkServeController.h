#pragma once

#include "BenchmarkConfig.h"

#include <string_view>

namespace psnr::benchmark
{
    class BenchmarkServeController final
    {
    public:
        [[nodiscard]] static int Run(const BenchmarkConfigV1& config, std::string_view normalizedConfigJson,
                                     std::string_view configPath);
    };
} // namespace psnr::benchmark
