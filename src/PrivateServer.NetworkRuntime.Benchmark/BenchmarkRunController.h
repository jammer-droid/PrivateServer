#pragma once

#include "BenchmarkConfig.h"

#include <string_view>

namespace psnr::benchmark
{
    class BenchmarkRunController final
    {
    public:
        // config : benchmark 설정값
        // normalizedConfigJson : config.json 으로 남기기 위한 config 를 파싱/검증한 json
        // configPath : server child가 해당 경로에 있는 config를 읽게 만들기 위한 경로
        [[nodiscard]] static int Run(const BenchmarkConfigV1& config, std::string_view normalizedConfigJson,
                                     std::string_view configPath);
    };
} // namespace psnr::benchmark
