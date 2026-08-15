#pragma once

#include <PrivateServer/NetworkRuntime/NrServerSnapshot.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// Command : Controller -> Server Child
// Event   : Controller <- Server Child

namespace psnr::benchmark
{
    enum class BenchmarkIpcEventType : std::uint8_t
    {
        Ready,
        RuntimeSample,
        Error,
        Stopped,
    };

    struct BenchmarkRuntimeSampleV1 final
    {
        psnr::runtime::NrServerLifecycleState lifecycleState = psnr::runtime::NrServerLifecycleState::Invalid;
        std::uint64_t registeredSessionCount = 0;
        std::uint64_t closingSessionCount = 0;
        std::uint64_t pendingRecvIoCount = 0;
        std::uint64_t pendingSendIoCount = 0;
        std::uint64_t toWorldEventDepth = 0;
        std::uint64_t toWorldEventHighWatermark = 0;
        std::array<std::uint64_t, psnr::runtime::NrPressureTransactionOutcomeCount> pressureTransactionCounts{};
        std::array<std::uint64_t, psnr::runtime::NrServerMemoryPoolRoleCount> poolExhaustionCounts{};
        std::array<psnr::runtime::NrServerMemoryPoolSnapshot, psnr::runtime::NrServerMemoryPoolRoleCount> memoryPools{};
        std::uint64_t totalPressureTransactions = 0;
        psnr::runtime::NrServerDiagnosticsSnapshot diagnostics;
    };

    struct BenchmarkIpcEventV1 final
    {
        BenchmarkIpcEventType type = BenchmarkIpcEventType::Ready;
        std::string runId;
        std::uint64_t sequence = 0;
        BenchmarkRuntimeSampleV1 runtimeSample;
        std::string errorMessage;
    };

    struct BenchmarkIpcEventEncodeResult final
    {
        std::string json;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    struct BenchmarkIpcEventDecodeResult final
    {
        BenchmarkIpcEventV1 event;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkIpcEventV1Codec final
    {
    public:
        [[nodiscard]] static BenchmarkIpcEventEncodeResult Encode(const BenchmarkIpcEventV1& event);
        [[nodiscard]] static BenchmarkIpcEventDecodeResult Decode(std::string_view jsonText);
    };
} // namespace psnr::benchmark
