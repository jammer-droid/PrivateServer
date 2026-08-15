#pragma once

#include "BenchmarkIpcPipe.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace psnr::benchmark
{
    using BenchmarkNativeProcessHandle = void*; // for server child process

    class BenchmarkServerChildProcess;

    struct BenchmarkServerChildLaunchResult final
    {
        std::unique_ptr<BenchmarkServerChildProcess> child;
        std::string error;
        std::uint32_t nativeErrorCode = 0;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return child != nullptr;
        }
    };

    struct BenchmarkServerChildExitResult final
    {
        std::string error;
        std::uint32_t nativeErrorCode = 0;
        std::uint32_t exitCode = 0;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error.empty();
        }
    };

    class BenchmarkServerChildProcess final
    {
    public:
        ~BenchmarkServerChildProcess() noexcept;

        BenchmarkServerChildProcess(const BenchmarkServerChildProcess&) = delete;
        BenchmarkServerChildProcess& operator=(const BenchmarkServerChildProcess&) = delete;

        BenchmarkServerChildProcess(BenchmarkServerChildProcess&&) = delete;
        BenchmarkServerChildProcess& operator=(BenchmarkServerChildProcess&&) = delete;

        [[nodiscard]] static BenchmarkServerChildLaunchResult Launch(std::string_view runId,
                                                                     std::string_view configPath);
        [[nodiscard]] static BenchmarkServerChildLaunchResult LaunchWorldHost(std::string_view executablePath,
                                                                              std::string_view runId,
                                                                              std::string_view configPath,
                                                                              std::string_view runsRoot);

        [[nodiscard]] BenchmarkNativeProcessHandle ProcessHandle() const noexcept;
        [[nodiscard]] BenchmarkIpcLineWriter& CommandWriter() noexcept;
        [[nodiscard]] BenchmarkIpcLineReader& EventReader() noexcept;
        [[nodiscard]] BenchmarkServerChildExitResult WaitForExit(std::uint32_t timeoutMilliseconds) const;

    private:
        enum class LaunchTarget : std::uint8_t
        {
            BenchmarkServerChild = 0,
            WorldServerHost,
        };

        BenchmarkServerChildProcess(BenchmarkNativeProcessHandle processHandle,
                                    BenchmarkIpcOwnedHandle&& commandWriteHandle,
                                    BenchmarkIpcOwnedHandle&& eventReadHandle) noexcept;

        [[nodiscard]] static BenchmarkServerChildLaunchResult LaunchProcess(LaunchTarget target,
                                                                            std::string_view executablePath,
                                                                            std::string_view runId,
                                                                            std::string_view configPath,
                                                                            std::string_view runsRoot);

        BenchmarkNativeProcessHandle processHandle_ = nullptr;
        BenchmarkIpcLineWriter commandWriter_; // Server Process에 IPC 를 통해 명령을 보냄
        BenchmarkIpcLineReader eventReader_;   // Server Process에서 IPC를 통해 이벤트를 보내고, 읽음
    };
} // namespace psnr::benchmark
