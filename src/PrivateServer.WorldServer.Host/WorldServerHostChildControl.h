#pragma once

#include "WorldResult.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace psnr::world::host
{
    struct WorldServerHostChildControlCommand final
    {
        std::uint64_t sequence = 0;
    };

    struct WorldServerHostChildControlFailure final
    {
        std::string message;
        std::uint32_t nativeErrorCode = 0;
    };

    class WorldServerHostChildControl final
    {
    public:
        ~WorldServerHostChildControl() noexcept;

        WorldServerHostChildControl(const WorldServerHostChildControl&) = delete;
        WorldServerHostChildControl& operator=(const WorldServerHostChildControl&) = delete;

        [[nodiscard]] static WorldResult<std::unique_ptr<WorldServerHostChildControl>,
                                         WorldServerHostChildControlFailure>
        Create(std::string runId, std::uint64_t commandReadHandle, std::uint64_t eventWriteHandle);

        [[nodiscard]] WorldResult<WorldServerHostChildControlCommand, WorldServerHostChildControlFailure> ReadCommand();
        [[nodiscard]] WorldResult<void, WorldServerHostChildControlFailure> WriteReady();
        [[nodiscard]] WorldResult<void, WorldServerHostChildControlFailure> WriteStopped(std::uint64_t stopSequence);
        [[nodiscard]] WorldResult<void, WorldServerHostChildControlFailure> WriteError(std::uint64_t sequence,
                                                                                       std::string_view message);

    private:
        WorldServerHostChildControl(std::string runId, void* commandReadHandle, void* eventWriteHandle) noexcept;

        [[nodiscard]] static bool IsValidNativeHandle(void* handle) noexcept;
        [[nodiscard]] static bool IsPipeHandle(void* handle) noexcept;
        [[nodiscard]] static WorldServerHostChildControlFailure Failure(std::string message,
                                                                        std::uint32_t nativeErrorCode = 0);
        [[nodiscard]] WorldResult<std::string, WorldServerHostChildControlFailure> ReadLine();
        [[nodiscard]] WorldResult<void, WorldServerHostChildControlFailure> WriteLine(std::string_view line);
        [[nodiscard]] WorldResult<void, WorldServerHostChildControlFailure> WriteEvent(
            std::string_view type, std::uint64_t sequence, std::string_view errorMessage = {});

        std::string runId_;
        void* commandReadHandle_ = nullptr;
        void* eventWriteHandle_ = nullptr;
        std::string pendingCommandBytes_;
    };
} // namespace psnr::world::host
