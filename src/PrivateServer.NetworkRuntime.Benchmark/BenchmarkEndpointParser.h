#pragma once

#include "BenchmarkConfig.h"

#include <PrivateServer/NetworkRuntime/NrEndpoint.h>

#include <cstdint>
#include <string_view>

namespace psnr::benchmark
{
    class BenchmarkEndpointParser final
    {
    public:
        [[nodiscard]] static bool TryParseServerEndpoint(const BenchmarkServerConfigV1& config,
                                                         psnr::runtime::NrEndpoint* outEndpoint) noexcept;
        [[nodiscard]] static bool TryParseServerEndpoint(std::string_view address, std::uint16_t port,
                                                         psnr::runtime::NrEndpoint* outEndpoint) noexcept;
    };
} // namespace psnr::benchmark
