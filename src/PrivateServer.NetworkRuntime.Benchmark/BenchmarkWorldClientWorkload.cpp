#include "BenchmarkWorldClientWorkload.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace psnr::benchmark
{
    namespace
    {
        constexpr std::int64_t NanosecondsPerSecond = 1'000'000'000;
        constexpr std::chrono::milliseconds DrainPollInterval{1};

        [[nodiscard]] std::uint32_t RemainingTimeoutMilliseconds(
            const std::chrono::steady_clock::time_point deadline) noexcept
        {
            const std::chrono::steady_clock::time_point observedAt = std::chrono::steady_clock::now();
            if (observedAt >= deadline)
            {
                return 0;
            }

            const std::int64_t remainingMilliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - observedAt).count();
            if (remainingMilliseconds <= 0)
            {
                return 1;
            }
            return static_cast<std::uint32_t>(std::min<std::int64_t>(
                remainingMilliseconds, static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())));
        }
    } // namespace

    BenchmarkWorldClientWorkload::~BenchmarkWorldClientWorkload() noexcept
    {
        static_cast<void>(Shutdown());
    }

    std::string BenchmarkWorldClientWorkload::StartAdmission(const psnr::runtime::NrEndpoint& endpoint,
                                                             const psnr::runtime::NrClientConfig& clientConfig,
                                                             const std::uint32_t clientCount,
                                                             const std::uint32_t rampPerSecond,
                                                             const std::uint32_t admissionTimeoutMilliseconds)
    {
        if (!clients_.empty() || drainThread_.joinable() || clientCount == 0 || rampPerSecond == 0 ||
            rampPerSecond > static_cast<std::uint32_t>(NanosecondsPerSecond) || admissionTimeoutMilliseconds == 0)
        {
            return "World client admission config or state is invalid";
        }

        clients_.reserve(clientCount);
        joinedClientCount_ = 0;
        completedClientCount_ = 0;
        sentControlCommandCount_ = 0;
        if (controlWorkloadActive_)
        {
            controlBots_.reserve(clientCount);
        }
        drainStopRequested_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock{mutex_};
            drainError_.clear();
        }
        try
        {
            drainThread_ = std::thread{&BenchmarkWorldClientWorkload::DrainEvents, this};
        }
        catch (const std::system_error& exception)
        {
            return std::string{"failed to start World client drain worker: "} + exception.what();
        }

        const std::chrono::nanoseconds rampInterval{NanosecondsPerSecond / rampPerSecond};
        const std::chrono::steady_clock::time_point admissionBegin = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point admissionDeadline =
            admissionBegin + std::chrono::milliseconds{admissionTimeoutMilliseconds};
        std::chrono::steady_clock::time_point nextClientAt = admissionBegin;

        for (std::uint32_t clientIndex = 0; clientIndex < clientCount; ++clientIndex)
        {
            std::this_thread::sleep_until(nextClientAt);
            const std::string drainError = DrainError();
            if (!drainError.empty())
            {
                static_cast<void>(Shutdown());
                return drainError;
            }
            const std::uint32_t remainingTimeoutMilliseconds = RemainingTimeoutMilliseconds(admissionDeadline);
            if (remainingTimeoutMilliseconds == 0)
            {
                const std::string shutdownError = Shutdown();
                return shutdownError.empty() ? "World client admission timed out" : shutdownError;
            }

            BenchmarkWorldClient client;
            const std::string startError = client.Start(endpoint, clientConfig, remainingTimeoutMilliseconds);
            if (!startError.empty())
            {
                const std::string clientShutdownError = client.Shutdown();
                const std::string workloadShutdownError = Shutdown();
                std::string admissionError = startError;
                if (!clientShutdownError.empty())
                {
                    admissionError.append("; client cleanup failed: ");
                    admissionError.append(clientShutdownError);
                }
                if (!workloadShutdownError.empty())
                {
                    admissionError.append("; workload cleanup failed: ");
                    admissionError.append(workloadShutdownError);
                }
                return admissionError;
            }

            {
                std::lock_guard<std::mutex> lock{mutex_};
                clients_.push_back(std::move(client));
                ++joinedClientCount_;
                if (controlWorkloadActive_)
                {
                    std::uint32_t clientSeed = workloadSeed_ + clientIndex;
                    if (clientSeed == 0)
                    {
                        clientSeed = 0x9E3779B9u;
                    }
                    controlBots_.push_back(ControlBotState{
                        clientSeed,
                        clients_.back().ControlledEntityGeneration(),
                        psnr::world::protocol::v2::TurnState::Straight,
                        psnr::world::protocol::v2::BoostState::Off,
                        std::chrono::steady_clock::now() + controlInterval_,
                    });
                }
            }
            nextClientAt += rampInterval;
        }

        const std::string drainError = DrainError();
        if (!drainError.empty())
        {
            static_cast<void>(Shutdown());
            return drainError;
        }
        return {};
    }

    std::string BenchmarkWorldClientWorkload::Shutdown() noexcept
    {
        const std::string drainError = StopEventDrain();

        std::lock_guard<std::mutex> lock{mutex_};
        std::string firstError = drainError;
        for (BenchmarkWorldClient& client : clients_)
        {
            if (client.HasRoundResult())
            {
                ++completedClientCount_;
            }
            const std::string shutdownError = client.Shutdown();
            if (firstError.empty() && !shutdownError.empty())
            {
                firstError = shutdownError;
            }
        }
        clients_.clear();
        controlBots_.clear();
        drainError_.clear();
        workloadSeed_ = 0;
        controlWorkloadActive_ = false;
        return firstError;
    }

    std::string BenchmarkWorldClientWorkload::StartControlWorkload(const std::uint32_t workloadSeed,
                                                                   const std::uint32_t controlIntervalMilliseconds,
                                                                   const std::uint32_t boostPercent)
    {
        if (controlIntervalMilliseconds == 0 || boostPercent > 100)
        {
            return "World control workload config is invalid";
        }

        std::lock_guard<std::mutex> lock{mutex_};
        if (!clients_.empty() || controlWorkloadActive_ || drainThread_.joinable())
        {
            return "World control workload state is invalid";
        }

        controlBots_.clear();
        controlInterval_ = std::chrono::milliseconds{controlIntervalMilliseconds};
        workloadSeed_ = workloadSeed;
        boostPercent_ = boostPercent;
        sentControlCommandCount_ = 0;
        controlWorkloadActive_ = true;
        return {};
    }

    std::string BenchmarkWorldClientWorkload::WaitForRoundResults(const std::uint32_t timeoutMilliseconds)
    {
        if (timeoutMilliseconds == 0)
        {
            return "World round result timeout must be greater than zero";
        }

        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{timeoutMilliseconds};
        std::unique_lock<std::mutex> lock{mutex_};
        if (clients_.empty() || !drainThread_.joinable())
        {
            return "World round result wait state is invalid";
        }

        const bool completed = progressWake_.wait_until(lock, deadline,
                                                        [this]() noexcept
                                                        {
                                                            return !drainError_.empty() ||
                                                                   AllRoundResultsCommittedLocked() ||
                                                                   drainStopRequested_.load(std::memory_order_acquire);
                                                        });
        if (!completed)
        {
            std::size_t completedClientCount = 0;
            for (const BenchmarkWorldClient& client : clients_)
            {
                if (client.HasRoundResult())
                {
                    ++completedClientCount;
                }
            }
            return "World round result wait timed out: completed=" + std::to_string(completedClientCount) + "/" +
                   std::to_string(clients_.size());
        }
        if (!drainError_.empty())
        {
            return drainError_;
        }
        if (!AllRoundResultsCommittedLocked())
        {
            return "World round result drain stopped before all clients completed";
        }
        return ValidateRoundResultsLocked();
    }

    std::string BenchmarkWorldClientWorkload::StopEventDrain() noexcept
    {
        drainStopRequested_.store(true, std::memory_order_release);
        progressWake_.notify_all();
        if (drainThread_.joinable())
        {
            drainThread_.join();
        }

        return DrainError();
    }

    std::size_t BenchmarkWorldClientWorkload::JoinedClientCount() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return joinedClientCount_;
    }

    std::size_t BenchmarkWorldClientWorkload::CompletedClientCount() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        std::size_t completedClientCount = 0;
        for (const BenchmarkWorldClient& client : clients_)
        {
            if (client.HasRoundResult())
            {
                ++completedClientCount;
            }
        }
        return std::max(completedClientCount_, completedClientCount);
    }

    std::uint64_t BenchmarkWorldClientWorkload::SentControlCommandCount() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return sentControlCommandCount_;
    }

    bool BenchmarkWorldClientWorkload::AllRoundResultsCommittedLocked() const noexcept
    {
        if (clients_.empty())
        {
            return false;
        }
        for (const BenchmarkWorldClient& client : clients_)
        {
            if (!client.HasRoundResult())
            {
                return false;
            }
        }
        return true;
    }

    std::string BenchmarkWorldClientWorkload::ValidateRoundResultsLocked() const
    {
        if (!AllRoundResultsCommittedLocked())
        {
            return "World round results are incomplete";
        }

        const psnr::world::protocol::v2::RoundResult& expected = clients_.front().CommittedRoundResult();
        for (std::size_t clientIndex = 1; clientIndex < clients_.size(); ++clientIndex)
        {
            const psnr::world::protocol::v2::RoundResult& observed = clients_[clientIndex].CommittedRoundResult();
            if (observed.roundId != expected.roundId || observed.endTick != expected.endTick ||
                observed.winningGrowthPoint != expected.winningGrowthPoint ||
                observed.winnerPlayerIds != expected.winnerPlayerIds)
            {
                return "World round result canonical fields differ: clientIndex=" + std::to_string(clientIndex);
            }
        }
        return {};
    }

    std::uint32_t BenchmarkWorldClientWorkload::NextRandom(std::uint32_t* const state) noexcept
    {
        std::uint32_t value = *state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        *state = value;
        return value;
    }

    std::string BenchmarkWorldClientWorkload::SendDueControlsLocked(
        const std::chrono::steady_clock::time_point observedAt)
    {
        if (!controlWorkloadActive_)
        {
            return {};
        }
        if (controlBots_.size() != clients_.size())
        {
            return "World control bot/client count is inconsistent";
        }

        for (std::size_t clientIndex = 0; clientIndex < clients_.size(); ++clientIndex)
        {
            BenchmarkWorldClient& client = clients_[clientIndex];
            ControlBotState& bot = controlBots_[clientIndex];
            if (client.HasRoundResult() || observedAt < bot.nextControlAt)
            {
                continue;
            }

            const std::uint32_t turnRoll = NextRandom(&bot.randomState) % 100;
            psnr::world::protocol::v2::TurnState turnState = psnr::world::protocol::v2::TurnState::Straight;
            if (turnRoll >= 75)
            {
                turnState = psnr::world::protocol::v2::TurnState::Right;
            }
            else if (turnRoll >= 50)
            {
                turnState = psnr::world::protocol::v2::TurnState::Left;
            }
            const psnr::world::protocol::v2::BoostState boostState = NextRandom(&bot.randomState) % 100 < boostPercent_
                                                                         ? psnr::world::protocol::v2::BoostState::On
                                                                         : psnr::world::protocol::v2::BoostState::Off;
            const bool generationChanged = bot.controlledEntityGeneration != client.ControlledEntityGeneration();
            if (generationChanged || turnState != bot.lastTurnState || boostState != bot.lastBoostState)
            {
                const std::string sendError = client.SendControlState(turnState, boostState);
                if (!sendError.empty())
                {
                    return sendError + ": clientIndex=" + std::to_string(clientIndex);
                }
                bot.controlledEntityGeneration = client.ControlledEntityGeneration();
                bot.lastTurnState = turnState;
                bot.lastBoostState = boostState;
                ++sentControlCommandCount_;
            }
            bot.nextControlAt = observedAt + controlInterval_;
        }
        return {};
    }

    void BenchmarkWorldClientWorkload::DrainEvents() noexcept
    {
        while (!drainStopRequested_.load(std::memory_order_acquire))
        {
            {
                std::lock_guard<std::mutex> lock{mutex_};
                for (BenchmarkWorldClient& client : clients_)
                {
                    const std::string drainError = client.DrainAvailableEvents();
                    if (!drainError.empty())
                    {
                        drainError_ = drainError;
                        drainStopRequested_.store(true, std::memory_order_release);
                        progressWake_.notify_all();
                        return;
                    }
                }
                const std::string controlError = SendDueControlsLocked(std::chrono::steady_clock::now());
                if (!controlError.empty())
                {
                    drainError_ = controlError;
                    drainStopRequested_.store(true, std::memory_order_release);
                    progressWake_.notify_all();
                    return;
                }
            }
            progressWake_.notify_all();
            std::this_thread::sleep_for(DrainPollInterval);
        }
    }

    std::string BenchmarkWorldClientWorkload::DrainError() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return drainError_;
    }
} // namespace psnr::benchmark
