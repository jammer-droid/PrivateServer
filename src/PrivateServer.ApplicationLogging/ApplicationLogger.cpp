#include "pch.h"

#include "ApplicationLogger.h"

#include "ApplicationLogEnvelope.h"
#include "ApplicationLogFanoutSink.h"
#include "ApplicationLogHandle.h"
#include "ApplicationLogOutputFactory.h"
#include "ApplicationLogPayloadCodec.h"
#include "ApplicationLoggerTestAccess.h"

#include <spdlog/async_logger.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/sink.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace psnr::logging
{
    namespace
    {
        constexpr std::size_t ApplicationLogWorkerCount = 1;
        constexpr std::string_view ApplicationLoggerName = "application";

        enum class ApplicationLoggerState : std::uint8_t
        {
            Created,
            Started,
            Stopped,
        };

        class ApplicationLogAsyncSink final : public spdlog::sinks::sink
        {
        public:
            explicit ApplicationLogAsyncSink(std::unique_ptr<internal::ApplicationLogFanoutSink> fanout)
                : fanout_(std::move(fanout))
            {
            }

            void log(const spdlog::details::log_msg& message) override
            {
                fanout_->Consume(std::string_view{message.payload.data(), message.payload.size()});
            }

            void flush() override
            {
                fanout_->Flush();
            }

            void set_pattern(const std::string&) override {}

            void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

            [[nodiscard]] internal::ApplicationLogFanoutSnapshot Snapshot() const noexcept
            {
                return fanout_->Snapshot();
            }

        private:
            std::unique_ptr<internal::ApplicationLogFanoutSink> fanout_;
        };

        class ActiveProducerGuard final
        {
        public:
            explicit ActiveProducerGuard(std::atomic<std::size_t>& activeProducers) noexcept
                : activeProducers_(activeProducers)
            {
            }

            ~ActiveProducerGuard() noexcept
            {
                activeProducers_.fetch_sub(1, std::memory_order_release);
            }

            ActiveProducerGuard(const ActiveProducerGuard&) = delete;
            ActiveProducerGuard& operator=(const ActiveProducerGuard&) = delete;

        private:
            std::atomic<std::size_t>& activeProducers_;
        };

        void AddSaturating(std::atomic<std::uint64_t>& counter, const std::uint64_t amount) noexcept
        {
            std::uint64_t current = counter.load(std::memory_order_relaxed);
            while (current != std::numeric_limits<std::uint64_t>::max())
            {
                const std::uint64_t remaining = std::numeric_limits<std::uint64_t>::max() - current;
                const std::uint64_t next =
                    amount >= remaining ? std::numeric_limits<std::uint64_t>::max() : current + amount;
                if (counter.compare_exchange_weak(current, next, std::memory_order_relaxed))
                {
                    return;
                }
            }
        }

        void IncrementSaturating(std::atomic<std::uint64_t>& counter) noexcept
        {
            AddSaturating(counter, 1);
        }

        [[nodiscard]] std::uint64_t ToUint64Saturating(const std::size_t value) noexcept
        {
            if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t))
            {
                if (value > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()))
                {
                    return std::numeric_limits<std::uint64_t>::max();
                }
            }

            return static_cast<std::uint64_t>(value);
        }

        [[nodiscard]] std::uint64_t CurrentSteadyNanoseconds() noexcept
        {
            const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now().time_since_epoch();
            const std::chrono::nanoseconds nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
            return nanoseconds.count() < 0 ? 0 : static_cast<std::uint64_t>(nanoseconds.count());
        }
    } // namespace

    class ApplicationLogger::Impl final
    {
    public:
        Impl(ApplicationLogConfig config, std::unique_ptr<internal::IApplicationLogOutput> fileOutput,
             std::unique_ptr<internal::IApplicationLogOutput> consoleOutput)
            : config_(std::move(config))
            , fileOutput_(std::move(fileOutput))
            , consoleOutput_(std::move(consoleOutput))
            , cachedFanoutSnapshot_{fileOutput_ == nullptr, consoleOutput_ == nullptr, 0, 0}
        {
        }

        [[nodiscard]] ApplicationLoggerResult Start() noexcept
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex_);
            if (state_ != ApplicationLoggerState::Created)
            {
                return ApplicationLoggerResult::InvalidState;
            }

            try
            {
                std::unique_ptr<internal::ApplicationLogFanoutSink> fanout =
                    std::make_unique<internal::ApplicationLogFanoutSink>(config_, std::move(fileOutput_),
                                                                         std::move(consoleOutput_));

                // spdlog's async API requires shared ownership for its thread pool, logger, and sink.
                asyncSink_ = std::make_shared<ApplicationLogAsyncSink>(std::move(fanout));

                // 로그 소비 및 출력을 위한 logging thread 1개 사용
                threadPool_ =
                    std::make_shared<spdlog::details::thread_pool>(config_.queueCapacity, ApplicationLogWorkerCount);

                // logger 가 logging worker 에서 asyncSink_ 를 호출하게 등록
                logger_ = std::make_shared<spdlog::async_logger>(
                    std::string{ApplicationLoggerName}, std::static_pointer_cast<spdlog::sinks::sink>(asyncSink_),
                    threadPool_, spdlog::async_overflow_policy::discard_new);
                logger_->set_level(spdlog::level::trace);

                state_ = ApplicationLoggerState::Started;
                accepting_.store(true, std::memory_order_release);
                return ApplicationLoggerResult::Success;
            }
            catch (...)
            {
                logger_.reset();
                threadPool_.reset();
                asyncSink_.reset();
                state_ = ApplicationLoggerState::Stopped;
                return ApplicationLoggerResult::ResourceUnavailable;
            }
        }

        void Log(const ApplicationLogRecord& record) noexcept
        {
            if (!TryEnterProducer()) // enter 시도할 때, activeProducers_ + 1
            {
                return;
            }

            // Log() 실행 도중 Shutdown 이 logger/thread pool 을 파괴하지 못하게 하기 위한 수명 보장
            // 현재 실행 중인 producer 만 추적
            const ActiveProducerGuard producerGuard{activeProducers_}; // 스코프 벗어나면 activeProducers_ -1
            IncrementSaturating(attempted_);

            try
            {
                if (!ApplicationLogRecord::IsValid(record) ||
                    static_cast<std::uint8_t>(record.severity) < static_cast<std::uint8_t>(config_.minimumSeverity))
                {
                    IncrementSaturating(filtered_);
                    return;
                }

                internal::ApplicationLogEnvelope envelope{};
                if (!internal::ApplicationLogEnvelope::TryCreate(record, std::chrono::system_clock::now(),
                                                                 CurrentSteadyNanoseconds(), &envelope))
                {
                    IncrementSaturating(filtered_);
                    return;
                }

                const std::string payload = internal::ApplicationLogPayloadCodec::Encode(envelope);
                logger_->log(spdlog::level::info, spdlog::string_view_t{payload.data(), payload.size()});
                rawQueueAttempts_.fetch_add(1, std::memory_order_release);
                ObserveBackendQueueCounters();
                ObserveQueueDepth();
            }
            catch (...)
            {
                // The public producer boundary is noexcept. Admission/normalization failures are rejected pre-queue.
                IncrementSaturating(filtered_);
            }
        }

        [[nodiscard]] ApplicationLogHealth CaptureHealth() const noexcept
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex_);

            ApplicationLogHealth health{};
            health.started = state_ == ApplicationLoggerState::Started;
            health.attempted = attempted_.load(std::memory_order_relaxed);
            health.filtered = filtered_.load(std::memory_order_relaxed);

            ObserveBackendQueueCounters();
            health.droppedQueueFull = droppedQueueFull_.load(std::memory_order_relaxed);
            health.enqueued = enqueued_.load(std::memory_order_relaxed);

            const internal::ApplicationLogFanoutSnapshot fanoutSnapshot = CurrentFanoutSnapshot();
            health.fileSinkFailed = fanoutSnapshot.fileSinkFailed;
            health.consoleSinkFailed = fanoutSnapshot.consoleSinkFailed;
            health.consumed = fanoutSnapshot.consumed;
            health.discardedAfterSinkFailure = fanoutSnapshot.discardedAfterSinkFailure;
            health.currentQueueDepth = threadPool_ == nullptr ? 0 : ToUint64Saturating(threadPool_->queue_size());
            health.maximumQueueDepth = maximumQueueDepth_.load(std::memory_order_relaxed);
            return health;
        }

        void Shutdown() noexcept
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex_);
            if (state_ == ApplicationLoggerState::Stopped)
            {
                return;
            }

            accepting_.store(false, std::memory_order_release);
            while (activeProducers_.load(std::memory_order_acquire) != 0) // producer 가 있으면 대기
            {
                std::this_thread::yield();
            }

            if (state_ == ApplicationLoggerState::Started)
            {
                bool flushPosted = false;
                try
                {
                    std::shared_ptr<spdlog::async_logger> flushLogger = logger_;
                    // block 방식으로 flush 제어 메시지를 queue에 추가
                    threadPool_->post_flush(std::move(flushLogger), spdlog::async_overflow_policy::block);
                    flushPosted = true;
                }
                catch (...)
                {
                }

                // 실행 중 누적된 queue-full 집계
                ObserveBackendQueueCounters();

                // thread pool 소멸자 실행, terminate 메시지 enqueue
                // worker 가 record/flush/terminate 순서로 pop 하고, join
                threadPool_.reset();
                if (!flushPosted)
                {
                    asyncSink_->flush(); // 필요시 asyncSink flush
                }
                cachedFanoutSnapshot_ = asyncSink_->Snapshot();
            }

            logger_.reset();
            asyncSink_.reset();
            state_ = ApplicationLoggerState::Stopped;
        }

    private:
        [[nodiscard]] bool TryEnterProducer() noexcept
        {
            if (!accepting_.load(std::memory_order_acquire))
            {
                return false;
            }

            activeProducers_.fetch_add(1, std::memory_order_acq_rel);
            if (!accepting_.load(std::memory_order_acquire))
            {
                activeProducers_.fetch_sub(1, std::memory_order_release);
                return false;
            }

            return true;
        }

        void ObserveQueueDepth() noexcept
        {
            const std::uint64_t queueDepth = ToUint64Saturating(threadPool_->queue_size());
            std::uint64_t maximum = maximumQueueDepth_.load(std::memory_order_relaxed);
            while (maximum < queueDepth &&
                   !maximumQueueDepth_.compare_exchange_weak(maximum, queueDepth, std::memory_order_relaxed))
            {
            }
        }

        void ObserveBackendQueueCounters() const noexcept
        {
            if (threadPool_ == nullptr)
            {
                return;
            }

            if (observingBackendQueue_.test_and_set(std::memory_order_acquire))
            {
                return;
            }

            const std::size_t queueAttempts = rawQueueAttempts_.load(std::memory_order_acquire);
            const std::size_t backendDiscarded = threadPool_->discard_counter();
            const std::size_t attemptDelta = queueAttempts - observedBackendQueueAttempts_;
            const std::size_t discardedDelta = backendDiscarded - observedBackendDiscarded_;
            if (discardedDelta <= attemptDelta)
            {
                observedBackendQueueAttempts_ = queueAttempts;
                observedBackendDiscarded_ = backendDiscarded;
                AddSaturating(droppedQueueFull_, ToUint64Saturating(discardedDelta));
                AddSaturating(enqueued_, ToUint64Saturating(attemptDelta - discardedDelta));
            }

            observingBackendQueue_.clear(std::memory_order_release);
        }

        [[nodiscard]] internal::ApplicationLogFanoutSnapshot CurrentFanoutSnapshot() const noexcept
        {
            return asyncSink_ == nullptr ? cachedFanoutSnapshot_ : asyncSink_->Snapshot();
        }

        ApplicationLogConfig config_;
        std::unique_ptr<internal::IApplicationLogOutput> fileOutput_;
        std::unique_ptr<internal::IApplicationLogOutput> consoleOutput_;

        mutable std::mutex lifecycleMutex_;
        ApplicationLoggerState state_ = ApplicationLoggerState::Created;
        std::atomic<bool> accepting_{false};
        std::atomic<std::size_t> activeProducers_{0};

        std::atomic<std::uint64_t> attempted_{0};
        std::atomic<std::uint64_t> filtered_{0};
        std::atomic<std::size_t> rawQueueAttempts_{0};
        mutable std::atomic_flag observingBackendQueue_ = ATOMIC_FLAG_INIT;
        mutable std::size_t observedBackendQueueAttempts_ = 0;
        mutable std::size_t observedBackendDiscarded_ = 0;
        mutable std::atomic<std::uint64_t> droppedQueueFull_{0};
        mutable std::atomic<std::uint64_t> enqueued_{0};
        std::atomic<std::uint64_t> maximumQueueDepth_{0};

        internal::ApplicationLogFanoutSnapshot cachedFanoutSnapshot_{};

        // logging worker 가 queue에서 꺼낸 payload 를 실제 프로젝트 로직으로 전달하기 위한 용도
        std::shared_ptr<ApplicationLogAsyncSink> asyncSink_;

        // logging worker 스레드 풀
        std::shared_ptr<spdlog::details::thread_pool> threadPool_;

        // producer 의 요청을 비동기 queue에 넣는 용도
        std::shared_ptr<spdlog::async_logger> logger_;
    };

    ApplicationLoggerResult ApplicationLogger::Create(const ApplicationLogConfig& config,
                                                      std::unique_ptr<ApplicationLogger>* const outLogger) noexcept
    {
        if (outLogger == nullptr)
        {
            return ApplicationLoggerResult::InvalidArgument;
        }

        if (!ApplicationLogConfig::IsValid(config))
        {
            return ApplicationLoggerResult::InvalidConfig;
        }

        std::unique_ptr<internal::IApplicationLogOutput> fileOutput;
        std::unique_ptr<internal::IApplicationLogOutput> consoleOutput;

        try
        {
            fileOutput = internal::ApplicationLogOutputFactory::CreateRotatingFile(config);
        }
        catch (...)
        {
        }

        try
        {
            consoleOutput = internal::ApplicationLogOutputFactory::CreateConsole();
        }
        catch (...)
        {
        }

        if (fileOutput == nullptr && consoleOutput == nullptr)
        {
            return ApplicationLoggerResult::OutputUnavailable;
        }

        std::unique_ptr<ApplicationLogger> logger =
            CreateWithOutputs(config, std::move(fileOutput), std::move(consoleOutput));
        if (logger == nullptr)
        {
            return ApplicationLoggerResult::ResourceUnavailable;
        }

        *outLogger = std::move(logger);
        return ApplicationLoggerResult::Success;
    }

    ApplicationLogger::~ApplicationLogger() noexcept
    {
        Shutdown();
    }

    ApplicationLoggerResult ApplicationLogger::Start() noexcept
    {
        return impl_->Start();
    }

    ApplicationLogHandle ApplicationLogger::CreateHandle() noexcept
    {
        return ApplicationLogHandle{*this};
    }

    void ApplicationLogger::Log(const ApplicationLogRecord& record) noexcept
    {
        impl_->Log(record);
    }

    ApplicationLogHealth ApplicationLogger::CaptureHealth() const noexcept
    {
        return impl_->CaptureHealth();
    }

    void ApplicationLogger::Shutdown() noexcept
    {
        impl_->Shutdown();
    }

    ApplicationLogHandle::ApplicationLogHandle(ApplicationLogger& logger) noexcept
        : logger_(logger)
    {
    }

    void ApplicationLogHandle::Log(const ApplicationLogRecord& record) const noexcept
    {
        logger_.Log(record);
    }

    ApplicationLogger::ApplicationLogger(std::unique_ptr<Impl> impl) noexcept
        : impl_(std::move(impl))
    {
    }

    std::unique_ptr<ApplicationLogger> ApplicationLogger::CreateWithOutputs(
        const ApplicationLogConfig& config, std::unique_ptr<internal::IApplicationLogOutput> fileOutput,
        std::unique_ptr<internal::IApplicationLogOutput> consoleOutput) noexcept
    {
        if (!ApplicationLogConfig::IsValid(config) || (fileOutput == nullptr && consoleOutput == nullptr))
        {
            return nullptr;
        }

        try
        {
            std::unique_ptr<Impl> impl =
                std::make_unique<Impl>(config, std::move(fileOutput), std::move(consoleOutput));
            return std::unique_ptr<ApplicationLogger>{new ApplicationLogger{std::move(impl)}};
        }
        catch (...)
        {
            return nullptr;
        }
    }
} // namespace psnr::logging

namespace psnr::logging::internal
{
    std::unique_ptr<ApplicationLogger> ApplicationLoggerTestAccess::Create(
        const ApplicationLogConfig& config, std::unique_ptr<IApplicationLogOutput> fileOutput,
        std::unique_ptr<IApplicationLogOutput> consoleOutput) noexcept
    {
        return ApplicationLogger::CreateWithOutputs(config, std::move(fileOutput), std::move(consoleOutput));
    }
} // namespace psnr::logging::internal
