#pragma once

#include "BenchmarkIpcEvent.h"

#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>

#include <string>

namespace psnr::benchmark
{
    struct BenchmarkRuntimeSampleProjectionResult final
    {
        BenchmarkRuntimeSampleV1 sample;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkRuntimeSampleProjection final
    {
    public:
        [[nodiscard]] static BenchmarkRuntimeSampleProjectionResult Project(
            const psnr::runtime::NrServerSnapshot& snapshot);
    };
} // namespace psnr::benchmark
