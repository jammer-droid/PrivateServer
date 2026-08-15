#include "WorldServerHostCommandLine.h"

#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>

namespace psnr::world::host
{
    namespace
    {
        constexpr std::string_view RunIdArgument = "--run-id";
        constexpr std::string_view ConfigArgument = "--config";
        constexpr std::string_view RunsRootArgument = "--runs-root";
        constexpr std::string_view CommandPipeHandleArgument = "--command-pipe-handle";
        constexpr std::string_view EventPipeHandleArgument = "--event-pipe-handle";
    } // namespace

    bool WorldServerHostCommandLine::HasValue(const int index, const int argumentCount,
                                              char* const arguments[]) noexcept
    {
        if (index + 1 >= argumentCount || arguments[index + 1] == nullptr)
        {
            return false;
        }

        const std::string_view value{arguments[index + 1]};
        return !value.empty() && !value.starts_with("--");
    }

    bool WorldServerHostCommandLine::TryParsePositiveHandle(const std::string_view text,
                                                            std::uint64_t* const outValue) noexcept
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

    WorldResult<WorldServerHostOptions> WorldServerHostCommandLine::Parse(const int argumentCount,
                                                                          char* const arguments[])
    {
        if (argumentCount < 1 || arguments == nullptr)
        {
            return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
        }

        WorldServerHostOptions options{};
        bool runIdSeen = false;
        bool configSeen = false;
        bool runsRootSeen = false;
        bool commandPipeHandleSeen = false;
        bool eventPipeHandleSeen = false;
        int index = 1;
        while (index < argumentCount)
        {
            if (arguments[index] == nullptr)
            {
                return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
            }

            const std::string_view argument{arguments[index]};
            if (argument == RunIdArgument)
            {
                if (runIdSeen || !HasValue(index, argumentCount, arguments))
                {
                    return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
                }

                options.requestedRunId = arguments[index + 1];
                runIdSeen = true;
                index += 2;
                continue;
            }
            if (argument == ConfigArgument)
            {
                if (configSeen || !HasValue(index, argumentCount, arguments))
                {
                    return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
                }

                options.configPath = arguments[index + 1];
                configSeen = true;
                index += 2;
                continue;
            }
            if (argument == RunsRootArgument)
            {
                if (runsRootSeen || !HasValue(index, argumentCount, arguments))
                {
                    return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
                }

                options.runsRoot = arguments[index + 1];
                if (!options.runsRoot.is_absolute())
                {
                    return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
                }
                runsRootSeen = true;
                index += 2;
                continue;
            }
            if (argument == CommandPipeHandleArgument)
            {
                if (commandPipeHandleSeen || !HasValue(index, argumentCount, arguments) ||
                    !TryParsePositiveHandle(arguments[index + 1], &options.commandPipeHandle))
                {
                    return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
                }

                commandPipeHandleSeen = true;
                index += 2;
                continue;
            }
            if (argument == EventPipeHandleArgument)
            {
                if (eventPipeHandleSeen || !HasValue(index, argumentCount, arguments) ||
                    !TryParsePositiveHandle(arguments[index + 1], &options.eventPipeHandle))
                {
                    return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
                }

                eventPipeHandleSeen = true;
                index += 2;
                continue;
            }

            return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
        }

        if (!configSeen)
        {
            return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
        }
        if (commandPipeHandleSeen != eventPipeHandleSeen)
        {
            return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
        }
        if (commandPipeHandleSeen && (!runIdSeen || !runsRootSeen))
        {
            return WorldResult<WorldServerHostOptions>::Failure(WorldErrorCode::InvalidArgument);
        }

        return WorldResult<WorldServerHostOptions>{std::move(options)};
    }
} // namespace psnr::world::host
