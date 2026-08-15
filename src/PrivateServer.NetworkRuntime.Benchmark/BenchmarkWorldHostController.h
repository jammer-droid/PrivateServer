#pragma once

#include "BenchmarkWorldHostControllerConfig.h"

#include <string_view>

namespace psnr::benchmark
{
    class BenchmarkWorldHostController final
    {
    public:
        [[nodiscard]] static int Run(const BenchmarkWorldHostControllerConfigV1& config,
                                     std::string_view normalizedConfigJson);
    };
} // namespace psnr::benchmark
