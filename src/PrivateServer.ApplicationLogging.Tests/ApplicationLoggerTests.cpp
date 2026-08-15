#include "pch.h"

#include "ApplicationLogHandle.h"
#include "ApplicationLogger.h"
#include "ApplicationLoggerTestAccess.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace psnr::logging
{
    static_assert(!std::is_default_constructible_v<ApplicationLogHandle>);
    static_assert(std::is_copy_constructible_v<ApplicationLogHandle>);
    static_assert(!std::is_copy_assignable_v<ApplicationLogHandle>);

    namespace
    {
        struct RecordingOutputState final
        {
            mutable std::mutex mutex;
            std::vector<std::string> payloads;
            std::atomic<std::size_t> flushCount{0};
        };

        class RecordingOutput final : public internal::IApplicationLogOutput
        {
        public:
            explicit RecordingOutput(RecordingOutputState* const state)
                : state_(state)
            {
            }

            void Write(const std::string_view payload) override
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->payloads.emplace_back(payload);
            }

            void Flush() override
            {
                state_->flushCount.fetch_add(1, std::memory_order_relaxed);
            }

        private:
            RecordingOutputState* state_;
        };

        struct BlockingOutputState final
        {
            mutable std::mutex mutex;
            std::condition_variable enteredCondition;
            std::condition_variable releaseCondition;
            bool entered = false;
            bool released = false;
            std::size_t writeCount = 0;
            std::size_t flushCount = 0;
        };

        class BlockingOutput final : public internal::IApplicationLogOutput
        {
        public:
            explicit BlockingOutput(BlockingOutputState* const state)
                : state_(state)
            {
            }

            void Write(std::string_view) override
            {
                std::unique_lock<std::mutex> lock(state_->mutex);
                state_->entered = true;
                state_->enteredCondition.notify_one();
                state_->releaseCondition.wait(lock, [this] { return state_->released; });
                ++state_->writeCount;
            }

            void Flush() override
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                ++state_->flushCount;
            }

        private:
            BlockingOutputState* state_;
        };

        [[nodiscard]] bool WaitUntilEntered(BlockingOutputState* const state)
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            return state->enteredCondition.wait_for(lock, std::chrono::seconds{5},
                                                    [state] { return state->entered; });
        }

        void Release(BlockingOutputState* const state)
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->released = true;
            state->releaseCondition.notify_one();
        }

        [[nodiscard]] ApplicationLogConfig ValidConfig(const std::size_t queueCapacity = 8)
        {
            ApplicationLogConfig config{};
            config.runId = "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000";
            config.process = "application_logger_tests";
            config.outputDirectory = "unused-test-output";
            config.queueCapacity = queueCapacity;
            return config;
        }

        [[nodiscard]] ApplicationLogRecord Record(const ApplicationLogSeverity severity, const std::string& event)
        {
            ApplicationLogRecord record{};
            record.severity = severity;
            record.component = "logging_test";
            record.event = event;
            return record;
        }
    } // namespace

    TEST(ApplicationLoggerTests, RejectsInvalidConfigAndMissingOutputs)
    {
        ApplicationLogConfig invalidConfig = ValidConfig();
        invalidConfig.queueCapacity = 0;
        RecordingOutputState outputState{};

        EXPECT_EQ(internal::ApplicationLoggerTestAccess::Create(
                      invalidConfig, std::make_unique<RecordingOutput>(&outputState), nullptr),
                  nullptr);
        EXPECT_EQ(internal::ApplicationLoggerTestAccess::Create(ValidConfig(), nullptr, nullptr), nullptr);
    }

    TEST(ApplicationLoggerTests, PublicCreateReturnsExplicitFailureAndPreservesOutput)
    {
        EXPECT_EQ(ApplicationLogger::Create(ValidConfig(), nullptr), ApplicationLoggerResult::InvalidArgument);

        RecordingOutputState outputState{};
        std::unique_ptr<ApplicationLogger> output = internal::ApplicationLoggerTestAccess::Create(
            ValidConfig(), std::make_unique<RecordingOutput>(&outputState), nullptr);
        ApplicationLogger* const original = output.get();
        ApplicationLogConfig invalidConfig = ValidConfig();
        invalidConfig.queueCapacity = 0;

        EXPECT_EQ(ApplicationLogger::Create(invalidConfig, &output), ApplicationLoggerResult::InvalidConfig);
        EXPECT_EQ(output.get(), original);
    }

    TEST(ApplicationLoggerTests, FiltersBeforeQueueAndDrainsAcceptedRecordsOnShutdown)
    {
        RecordingOutputState fileState{};
        RecordingOutputState consoleState{};
        std::unique_ptr<RecordingOutput> fileOutput = std::make_unique<RecordingOutput>(&fileState);
        std::unique_ptr<RecordingOutput> consoleOutput = std::make_unique<RecordingOutput>(&consoleState);
        std::unique_ptr<ApplicationLogger> logger = internal::ApplicationLoggerTestAccess::Create(
            ValidConfig(), std::move(fileOutput), std::move(consoleOutput));

        ASSERT_NE(logger, nullptr);
        logger->Log(Record(ApplicationLogSeverity::Info, "before_start"));
        ASSERT_EQ(logger->Start(), ApplicationLoggerResult::Success);
        EXPECT_EQ(logger->Start(), ApplicationLoggerResult::InvalidState);

        logger->Log(Record(ApplicationLogSeverity::Debug, "filtered"));
        logger->Log(Record(ApplicationLogSeverity::Info, "accepted"));
        logger->Shutdown();
        logger->Log(Record(ApplicationLogSeverity::Info, "after_shutdown"));
        logger->Shutdown();

        const ApplicationLogHealth health = logger->CaptureHealth();
        EXPECT_FALSE(health.started);
        EXPECT_FALSE(health.fileSinkFailed);
        EXPECT_FALSE(health.consoleSinkFailed);
        EXPECT_EQ(health.attempted, 2);
        EXPECT_EQ(health.filtered, 1);
        EXPECT_EQ(health.enqueued, 1);
        EXPECT_EQ(health.consumed, 1);
        EXPECT_EQ(health.droppedQueueFull, 0);
        EXPECT_EQ(health.discardedAfterSinkFailure, 0);
        EXPECT_EQ(health.currentQueueDepth, 0);
        EXPECT_EQ(fileState.payloads.size(), 1);
        EXPECT_EQ(consoleState.payloads.size(), 1);
        EXPECT_EQ(fileState.flushCount.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(consoleState.flushCount.load(std::memory_order_relaxed), 1);
    }

    TEST(ApplicationLoggerTests, DiscardsNewestRecordWithoutWaitingWhenQueueIsFull)
    {
        BlockingOutputState fileState{};
        std::unique_ptr<BlockingOutput> fileOutput = std::make_unique<BlockingOutput>(&fileState);
        std::unique_ptr<ApplicationLogger> logger = internal::ApplicationLoggerTestAccess::Create(
            ValidConfig(1), std::move(fileOutput), nullptr);

        ASSERT_NE(logger, nullptr);
        ASSERT_EQ(logger->Start(), ApplicationLoggerResult::Success);

        logger->Log(Record(ApplicationLogSeverity::Info, "blocks_consumer"));
        if (!WaitUntilEntered(&fileState))
        {
            Release(&fileState);
            logger->Shutdown();
            FAIL() << "async consumer did not enter the controlled output";
        }

        logger->Log(Record(ApplicationLogSeverity::Info, "fills_queue"));
        const std::chrono::steady_clock::time_point beforeDrop = std::chrono::steady_clock::now();
        logger->Log(Record(ApplicationLogSeverity::Info, "discarded_newest"));
        const std::chrono::steady_clock::duration dropDuration = std::chrono::steady_clock::now() - beforeDrop;

        const ApplicationLogHealth saturatedHealth = logger->CaptureHealth();
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(dropDuration).count(), 1'000);
        EXPECT_EQ(saturatedHealth.attempted, 3);
        EXPECT_EQ(saturatedHealth.enqueued, 2);
        EXPECT_EQ(saturatedHealth.droppedQueueFull, 1);
        EXPECT_EQ(saturatedHealth.currentQueueDepth, 1);
        EXPECT_EQ(saturatedHealth.maximumQueueDepth, 1);

        Release(&fileState);
        logger->Shutdown();

        const ApplicationLogHealth shutdownHealth = logger->CaptureHealth();
        EXPECT_EQ(shutdownHealth.attempted,
                  shutdownHealth.filtered + shutdownHealth.enqueued + shutdownHealth.droppedQueueFull);
        EXPECT_EQ(shutdownHealth.enqueued,
                  shutdownHealth.consumed + shutdownHealth.discardedAfterSinkFailure);
        EXPECT_EQ(shutdownHealth.consumed, 2);
        EXPECT_EQ(fileState.writeCount, 2);
        EXPECT_EQ(fileState.flushCount, 1);
    }

    TEST(ApplicationLoggerTests, ReportsDegradedStartupWhenOnlyOneOutputExists)
    {
        RecordingOutputState consoleState{};
        std::unique_ptr<ApplicationLogger> logger = internal::ApplicationLoggerTestAccess::Create(
            ValidConfig(), nullptr, std::make_unique<RecordingOutput>(&consoleState));

        ASSERT_NE(logger, nullptr);
        ASSERT_EQ(logger->Start(), ApplicationLoggerResult::Success);

        const ApplicationLogHealth health = logger->CaptureHealth();
        EXPECT_TRUE(health.started);
        EXPECT_TRUE(health.fileSinkFailed);
        EXPECT_FALSE(health.consoleSinkFailed);

        logger->Shutdown();
    }

    TEST(ApplicationLoggerTests, BorrowedHandleCopiesShareTheHostOwnedLogger)
    {
        RecordingOutputState fileState{};
        std::unique_ptr<ApplicationLogger> logger = internal::ApplicationLoggerTestAccess::Create(
            ValidConfig(), std::make_unique<RecordingOutput>(&fileState), nullptr);

        ASSERT_NE(logger, nullptr);
        ASSERT_EQ(logger->Start(), ApplicationLoggerResult::Success);

        const ApplicationLogHandle firstHandle = logger->CreateHandle();
        const ApplicationLogHandle copiedHandle = firstHandle;
        firstHandle.Log(Record(ApplicationLogSeverity::Info, "first_handle"));
        copiedHandle.Log(Record(ApplicationLogSeverity::Info, "copied_handle"));
        logger->Shutdown();

        const ApplicationLogHealth health = logger->CaptureHealth();
        EXPECT_EQ(health.attempted, 2);
        EXPECT_EQ(health.enqueued, 2);
        EXPECT_EQ(health.consumed, 2);
        EXPECT_EQ(fileState.payloads.size(), 2);
    }
} // namespace psnr::logging
