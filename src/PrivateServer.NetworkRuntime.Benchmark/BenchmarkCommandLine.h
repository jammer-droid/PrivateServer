#pragma once

#include <cstdint>
#include <string>

namespace psnr::benchmark
{
    enum class BenchmarkMode : std::uint8_t
    {
        Run,
        Serve,
        WorldHostLifecycle,
        ServerChild,
    };

    struct BenchmarkCommandLineOptions final
    {
        BenchmarkMode mode = BenchmarkMode::Run;
        std::string configPath;
        std::string runId;
        std::uint64_t commandPipeHandle = 0;
        std::uint64_t eventPipeHandle = 0;
    };

    struct BenchmarkCommandLineParseResult final
    {
        BenchmarkCommandLineOptions options;
        std::string error;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkCommandLineParser final
    {
    public:
        [[nodiscard]] static BenchmarkCommandLineParseResult Parse(int argumentCount, char* const arguments[]);
        [[nodiscard]] static const char* Usage() noexcept;
    };
} // namespace psnr::benchmark
