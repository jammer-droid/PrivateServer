#pragma once

#include <PrivateServer/NetworkRuntime/NrClient.h>
#include <PrivateServer/NetworkRuntime/NrClientEvent.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>

#include <chrono>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    class BenchmarkClientTransport final
    {
    public:
        [[nodiscard]] static std::string DescribeFailure(std::string_view operation, psnr::core::NrStatus status);
        [[nodiscard]] static std::string TryReadNextEvent(psnr::runtime::NrClient& client,
                                                          psnr::runtime::NrClientEvent* outEvent, bool* outEventRead);
        [[nodiscard]] static std::string ReadNextEventUntil(psnr::runtime::NrClient& client,
                                                            std::chrono::steady_clock::time_point deadline,
                                                            psnr::runtime::NrClientEvent* outEvent);
    };
} // namespace psnr::benchmark
