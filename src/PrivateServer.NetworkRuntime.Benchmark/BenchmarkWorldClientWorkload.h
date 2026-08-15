#pragma once

#include "BenchmarkWorldClient.h"

#include <PrivateServer/NetworkRuntime/NrEndpoint.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace psnr::benchmark
{
    class BenchmarkWorldClientWorkload final
    {
    public:
        BenchmarkWorldClientWorkload() = default;
        ~BenchmarkWorldClientWorkload() noexcept;

        BenchmarkWorldClientWorkload(const BenchmarkWorldClientWorkload&) = delete;
        BenchmarkWorldClientWorkload& operator=(const BenchmarkWorldClientWorkload&) = delete;

        BenchmarkWorldClientWorkload(BenchmarkWorldClientWorkload&&) = delete;
        BenchmarkWorldClientWorkload& operator=(BenchmarkWorldClientWorkload&&) = delete;

        [[nodiscard]] std::string StartAdmission(const psnr::runtime::NrEndpoint& endpoint,
                                                 const psnr::runtime::NrClientConfig& clientConfig,
                                                 std::uint32_t clientCount, std::uint32_t rampPerSecond,
                                                 std::uint32_t admissionTimeoutMilliseconds);
        [[nodiscard]] std::string StartControlWorkload(std::uint32_t workloadSeed,
                                                       std::uint32_t controlIntervalMilliseconds,
                                                       std::uint32_t boostPercent);
        [[nodiscard]] std::string WaitForRoundResults(std::uint32_t timeoutMilliseconds);
        [[nodiscard]] std::string StopEventDrain() noexcept;
        [[nodiscard]] std::string Shutdown() noexcept;

        [[nodiscard]] std::size_t JoinedClientCount() const noexcept;
        [[nodiscard]] std::size_t CompletedClientCount() const noexcept;
        [[nodiscard]] std::uint64_t SentControlCommandCount() const noexcept;

    private:
        struct ControlBotState final
        {
            std::uint32_t randomState = 0;
            std::uint32_t controlledEntityGeneration = 0;
            psnr::world::protocol::v2::TurnState lastTurnState = psnr::world::protocol::v2::TurnState::Straight;
            psnr::world::protocol::v2::BoostState lastBoostState = psnr::world::protocol::v2::BoostState::Off;
            std::chrono::steady_clock::time_point nextControlAt{};
        };

        void DrainEvents() noexcept;
        [[nodiscard]] std::string SendDueControlsLocked(std::chrono::steady_clock::time_point observedAt);
        [[nodiscard]] static std::uint32_t NextRandom(std::uint32_t* state) noexcept;
        [[nodiscard]] std::string DrainError() const;
        [[nodiscard]] bool AllRoundResultsCommittedLocked() const noexcept;
        [[nodiscard]] std::string ValidateRoundResultsLocked() const;

        mutable std::mutex mutex_;
        std::condition_variable progressWake_;
        std::vector<BenchmarkWorldClient> clients_;
        std::vector<ControlBotState> controlBots_;
        std::thread drainThread_;
        std::string drainError_;
        std::chrono::milliseconds controlInterval_{0};
        std::uint32_t workloadSeed_ = 0;
        std::uint32_t boostPercent_ = 0;
        std::size_t joinedClientCount_ = 0;
        std::size_t completedClientCount_ = 0;
        std::uint64_t sentControlCommandCount_ = 0;
        bool controlWorkloadActive_ = false;
        std::atomic<bool> drainStopRequested_ = false;
    };
} // namespace psnr::benchmark
