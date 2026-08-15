#include "BenchmarkCommandLine.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::string_view RunModeArgument = "run";
        constexpr std::string_view ServeModeArgument = "serve";
        constexpr std::string_view WorldHostLifecycleModeArgument = "world-host-lifecycle";
        constexpr std::string_view ServerChildModeArgument = "--server-child";
        constexpr std::string_view ConfigArgument = "--config";
        constexpr std::string_view RunIdArgument = "--run-id";
        constexpr std::string_view CommandPipeHandleArgument = "--command-pipe-handle";
        constexpr std::string_view EventPipeHandleArgument = "--event-pipe-handle";
        constexpr std::size_t MaximumRunIdBytes = 128;

        [[nodiscard]] BenchmarkCommandLineParseResult Failure(std::string error)
        {
            BenchmarkCommandLineParseResult result;
            result.error = std::move(error);
            return result;
        }

        [[nodiscard]] bool ParsePositiveHandle(const std::string_view text, std::uint64_t* outValue) noexcept
        {
            if (text.empty() || outValue == nullptr)
            {
                return false;
            }

            std::uint64_t value = 0;
            const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
            {
                return false;
            }

            *outValue = value;
            return true;
        }

        [[nodiscard]] bool HasValue(const int argumentIndex, const int argumentCount, char* const arguments[]) noexcept
        {
            if (argumentIndex + 1 >= argumentCount || arguments[argumentIndex + 1] == nullptr)
            {
                return false;
            }

            const std::string_view value(arguments[argumentIndex + 1]);
            return !value.empty() && !value.starts_with("--");
        }
    } // namespace

    BenchmarkCommandLineParseResult BenchmarkCommandLineParser::Parse(const int argumentCount, char* const arguments[])
    {
        if (argumentCount < 2 || arguments == nullptr || arguments[1] == nullptr)
        {
            return Failure("benchmark mode is required");
        }

        BenchmarkCommandLineOptions options;
        const std::string_view modeArgument(arguments[1]);
        if (modeArgument == RunModeArgument)
        {
            options.mode = BenchmarkMode::Run;
        }
        else if (modeArgument == ServeModeArgument)
        {
            options.mode = BenchmarkMode::Serve;
        }
        else if (modeArgument == WorldHostLifecycleModeArgument)
        {
            options.mode = BenchmarkMode::WorldHostLifecycle;
        }
        else if (modeArgument == ServerChildModeArgument)
        {
            options.mode = BenchmarkMode::ServerChild;
        }
        else
        {
            return Failure("unknown benchmark mode: " + std::string(modeArgument));
        }

        // prevent using same args flags
        bool hasConfigPath = false;
        bool hasRunId = false;
        bool hasCommandPipeHandle = false;
        bool hasEventPipeHandle = false;
        for (int argumentIndex = 2; argumentIndex < argumentCount; ++argumentIndex)
        {
            if (arguments[argumentIndex] == nullptr)
            {
                return Failure("command-line argument must not be null");
            }

            const std::string_view argument(arguments[argumentIndex]);
            if (argument == ConfigArgument)
            {
                if (hasConfigPath)
                {
                    return Failure("--config must not be repeated");
                }
                if (!HasValue(argumentIndex, argumentCount, arguments))
                {
                    return Failure("--config requires a path");
                }

                options.configPath = arguments[++argumentIndex];
                hasConfigPath = true;
                continue;
            }

            if (argument == RunIdArgument)
            {
                if (hasRunId)
                {
                    return Failure("--run-id must not be repeated");
                }
                if (!HasValue(argumentIndex, argumentCount, arguments))
                {
                    return Failure("--run-id requires a value");
                }

                options.runId = arguments[++argumentIndex];
                if (options.runId.size() > MaximumRunIdBytes)
                {
                    return Failure("--run-id must contain between 1 and 128 bytes");
                }
                hasRunId = true;
                continue;
            }

            if (argument == CommandPipeHandleArgument)
            {
                if (hasCommandPipeHandle)
                {
                    return Failure("--command-pipe-handle must not be repeated");
                }
                if (!HasValue(argumentIndex, argumentCount, arguments) ||
                    !ParsePositiveHandle(arguments[argumentIndex + 1], &options.commandPipeHandle))
                {
                    return Failure("--command-pipe-handle requires a positive integer");
                }

                ++argumentIndex;
                hasCommandPipeHandle = true;
                continue;
            }

            if (argument == EventPipeHandleArgument)
            {
                if (hasEventPipeHandle)
                {
                    return Failure("--event-pipe-handle must not be repeated");
                }
                if (!HasValue(argumentIndex, argumentCount, arguments) ||
                    !ParsePositiveHandle(arguments[argumentIndex + 1], &options.eventPipeHandle))
                {
                    return Failure("--event-pipe-handle requires a positive integer");
                }

                ++argumentIndex;
                hasEventPipeHandle = true;
                continue;
            }

            return Failure("unknown argument: " + std::string(argument));
        }

        if (options.mode == BenchmarkMode::ServerChild)
        {
            if (!hasRunId || !hasCommandPipeHandle || !hasEventPipeHandle)
            {
                return Failure("--server-child requires runId and both pipe handles");
            }
        }
        else if (hasRunId || hasCommandPipeHandle || hasEventPipeHandle)
        {
            return Failure("runId and pipe handle arguments are valid only for --server-child");
        }
        if (options.mode == BenchmarkMode::WorldHostLifecycle && !hasConfigPath)
        {
            return Failure("world-host-lifecycle requires --config");
        }

        BenchmarkCommandLineParseResult result;
        result.options = std::move(options);
        return result;
    }

    const char* BenchmarkCommandLineParser::Usage() noexcept
    {
        return "Usage:\n"
               "  PrivateServer.NetworkRuntime.Benchmark.exe run [--config <path>]\n"
               "  PrivateServer.NetworkRuntime.Benchmark.exe serve [--config <path>]\n"
               "  PrivateServer.NetworkRuntime.Benchmark.exe world-host-lifecycle --config <path>\n"
               "  PrivateServer.NetworkRuntime.Benchmark.exe --server-child "
               "--run-id <value> --command-pipe-handle <value> --event-pipe-handle <value> [--config <path>]";
    }
} // namespace psnr::benchmark
