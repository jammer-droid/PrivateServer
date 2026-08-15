#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Command : Controller -> Server Child
// Event   : Controller <- Server Child

namespace psnr::benchmark
{
    enum class BenchmarkIpcCommandType : std::uint8_t
    {
        CaptureSnapshot,
        Stop,
    };

    struct BenchmarkIpcCommandV1 final
    {
        BenchmarkIpcCommandType type = BenchmarkIpcCommandType::CaptureSnapshot;
        std::string runId;
        std::uint64_t sequence = 0;

        [[nodiscard]] friend bool operator==(const BenchmarkIpcCommandV1& left,
                                             const BenchmarkIpcCommandV1& right) noexcept = default;
    };

    struct BenchmarkIpcCommandEncodeResult final
    {
        std::string json;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    struct BenchmarkIpcCommandDecodeResult final
    {
        BenchmarkIpcCommandV1 command;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkIpcCommandV1Codec final
    {
    public:
        [[nodiscard]] static BenchmarkIpcCommandEncodeResult Encode(const BenchmarkIpcCommandV1& command);
        [[nodiscard]] static BenchmarkIpcCommandDecodeResult Decode(std::string_view jsonText);
    };
} // namespace psnr::benchmark
