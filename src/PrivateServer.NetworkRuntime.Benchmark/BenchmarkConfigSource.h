#pragma once

#include "BenchmarkConfig.h"

#include <string>
#include <string_view>

namespace psnr::benchmark
{
    struct BenchmarkResolvedConfig final
    {
        BenchmarkConfigV1 config;
        std::string normalizedJson; // config를 JSON 으로 직렬화. benchmark run 에 함께 기록하는 용도
        bool loadedFromFile = false;
    };

    struct BenchmarkConfigResolveResult final
    {
        BenchmarkResolvedConfig resolved;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkConfigSource final
    {
    public:
        [[nodiscard]] static BenchmarkConfigResolveResult Resolve(std::string_view configPath);
    };
} // namespace psnr::benchmark
