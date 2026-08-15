#pragma once

#include <PrivateServer/NetworkRuntime/NrGateway.h>
#include <PrivateServer/NetworkRuntime/NrSessionKey.h>

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace psnr::runtime
{
    class NrServer;
    class NrToWorldEvent;
} // namespace psnr::runtime

namespace psnr::benchmark
{
    class BenchmarkServerEchoWorker final
    {
    public:
        BenchmarkServerEchoWorker() noexcept = default;
        ~BenchmarkServerEchoWorker() noexcept;

        BenchmarkServerEchoWorker(const BenchmarkServerEchoWorker&) = delete;
        BenchmarkServerEchoWorker& operator=(const BenchmarkServerEchoWorker&) = delete;

        BenchmarkServerEchoWorker(BenchmarkServerEchoWorker&&) = delete;
        BenchmarkServerEchoWorker& operator=(BenchmarkServerEchoWorker&&) = delete;

        [[nodiscard]] bool Start(psnr::runtime::NrServer* server, std::string* outError);

        // Stops new Echo submissions while the worker keeps draining ToWorld events produced during Runtime shutdown.
        void BeginDrainOnly() noexcept;

        // Call after NrServer::Shutdown(). The worker drains the final queue contents and then joins.
        void DrainRemainingAndJoin() noexcept;

        [[nodiscard]] bool TryGetFailure(std::string* outError) const;

    private:
        void ThreadMain() noexcept;
        [[nodiscard]] bool ProcessEvent(psnr::runtime::NrToWorldEvent* event);
        void RecordFailure(std::string_view error) noexcept;

        psnr::runtime::NrServer* server_ = nullptr;
        psnr::runtime::NrGateway gateway_;
        std::unordered_map<psnr::core::NrSessionKey, psnr::runtime::NrSessionSendChannel> activeSendChannels_;
        std::thread thread_;
        std::atomic_bool drainOnly_{false};
        std::atomic_bool stopAfterDrain_{false};
        std::atomic_bool failed_{false};
        mutable std::mutex failureMutex_;
        std::string failure_;
    };
} // namespace psnr::benchmark
