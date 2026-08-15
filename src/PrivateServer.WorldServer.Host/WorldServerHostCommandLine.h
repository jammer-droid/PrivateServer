#pragma once

#include "WorldResult.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace psnr::world::host
{
    struct WorldServerHostOptions final
    {
        std::string requestedRunId;
        std::filesystem::path configPath;
        std::filesystem::path runsRoot;
        std::uint64_t commandPipeHandle = 0;
        std::uint64_t eventPipeHandle = 0;

        [[nodiscard]] bool IsChildControlled() const noexcept
        {
            return commandPipeHandle != 0 && eventPipeHandle != 0;
        }
    };

    class WorldServerHostCommandLine final
    {
    public:
        [[nodiscard]] static WorldResult<WorldServerHostOptions> Parse(int argumentCount, char* const arguments[]);

    private:
        [[nodiscard]] static bool HasValue(int index, int argumentCount, char* const arguments[]) noexcept;
        [[nodiscard]] static bool TryParsePositiveHandle(std::string_view text, std::uint64_t* outValue) noexcept;
    };
} // namespace psnr::world::host
