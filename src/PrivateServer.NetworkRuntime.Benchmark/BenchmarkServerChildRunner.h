#pragma once

#include "BenchmarkConfig.h"

#include <cstdint>
#include <string_view>

namespace psnr::benchmark
{
    class BenchmarkServerChildRunner final
    {
    public:
        [[nodiscard]] static int Run(std::string_view runId, const BenchmarkServerConfigV1& serverConfig,
                                     const BenchmarkArtifactConfigV1& artifactConfig,
                                     std::uint64_t commandPipeHandleValue, std::uint64_t eventPipeHandleValue);
    };
} // namespace psnr::benchmark
