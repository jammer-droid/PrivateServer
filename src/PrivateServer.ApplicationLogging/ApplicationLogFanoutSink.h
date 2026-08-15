#pragma once

#include "ApplicationLogConfig.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace psnr::logging::internal
{
    struct ApplicationLogFanoutSnapshot final
    {
        bool fileSinkFailed = false;
        bool consoleSinkFailed = false;
        std::uint64_t consumed = 0;
        std::uint64_t discardedAfterSinkFailure = 0;
    };

    class IApplicationLogOutput
    {
    public:
        virtual ~IApplicationLogOutput() = default;

        virtual void Write(std::string_view payload) = 0;
        virtual void Flush() = 0;
    };

    // Runs behind the single-worker async queue. Only Snapshot() is called concurrently.
    class ApplicationLogFanoutSink final
    {
    public:
        ApplicationLogFanoutSink(ApplicationLogConfig config, std::unique_ptr<IApplicationLogOutput> fileOutput,
                                 std::unique_ptr<IApplicationLogOutput> consoleOutput);

        void Consume(std::string_view transportPayload);
        void Flush() noexcept;

        [[nodiscard]] ApplicationLogFanoutSnapshot Snapshot() const noexcept;

    private:
        static void IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept;
        static void WriteIfHealthy(IApplicationLogOutput* output, std::atomic<bool>& failed,
                                   const std::string& payload) noexcept;
        static void FlushIfHealthy(IApplicationLogOutput* output, std::atomic<bool>& failed) noexcept;

        [[nodiscard]] std::uint64_t NextDrainSequence() noexcept;

        ApplicationLogConfig config_;

        std::unique_ptr<IApplicationLogOutput> fileOutput_;
        std::unique_ptr<IApplicationLogOutput> consoleOutput_;

        // sinkFailed 가 true 이면 해당 sink 기록을 중단함
        std::atomic<bool> fileSinkFailed_{false};
        std::atomic<bool> consoleSinkFailed_{false};

        // record dequeue -> fild/console fanout 이후 +1
        std::atomic<std::uint64_t> consumed_{0};
        // file, console sink 가 모두 이미 실패한 상태에서 다음 record 도착하면 discard +1
        std::atomic<std::uint64_t> discardedAfterSinkFailure_{0};

        std::uint64_t drainSequence_ = 0;
    };
} // namespace psnr::logging::internal
