#include "pch.h"

#include "ApplicationLogFanoutSink.h"

#include "ApplicationLogEnvelope.h"
#include "ApplicationLogPayloadCodec.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psnr::logging::internal
{
    namespace
    {
        using ApplicationLogJson = nlohmann::json;

        class RecordingOutput final : public IApplicationLogOutput
        {
        public:
            void Write(const std::string_view payload) override
            {
                ++writeCallCount;
                if (throwOnWrite)
                {
                    throw std::runtime_error("controlled write failure");
                }

                payloads.emplace_back(payload);
            }

            void Flush() override
            {
                ++flushCallCount;
                if (throwOnFlush)
                {
                    throw std::runtime_error("controlled flush failure");
                }
            }

            bool throwOnWrite = false;
            bool throwOnFlush = false;
            std::size_t writeCallCount = 0;
            std::size_t flushCallCount = 0;
            std::vector<std::string> payloads;
        };

        [[nodiscard]] std::unique_ptr<RecordingOutput> MakeRecordingOutput(RecordingOutput** const observer)
        {
            std::unique_ptr<RecordingOutput> output = std::make_unique<RecordingOutput>();
            *observer = output.get();
            return output;
        }

        [[nodiscard]] ApplicationLogConfig MakeConfig()
        {
            ApplicationLogConfig config{};
            config.runId = "run-20260801T153012.482Z-550e8400-e29b-41d4-a716-446655440000";
            config.process = "world-server-host";
            config.outputDirectory = "artifacts/runs/test/world";
            return config;
        }

        [[nodiscard]] std::string MakeTransportPayload(const std::uint64_t producerSteadyNs)
        {
            ApplicationLogRecord record{};
            record.severity = ApplicationLogSeverity::Warning;
            record.component = "world";
            record.event = "worker_stopped";

            ApplicationLogEnvelope envelope{};
            const bool created = ApplicationLogEnvelope::TryCreate(
                record, std::chrono::system_clock::time_point{std::chrono::milliseconds{1'785'598'212'482}},
                producerSteadyNs, &envelope);
            EXPECT_TRUE(created);
            return ApplicationLogPayloadCodec::Encode(envelope);
        }

        void Submit(ApplicationLogFanoutSink* const fanout, const std::string& transportPayload)
        {
            fanout->Consume(transportPayload);
        }
    } // namespace

    TEST(ApplicationLogFanoutSinkTests, ProjectsOneEnvelopeToBothChannelsWithOneDrainSequence)
    {
        RecordingOutput* fileOutput = nullptr;
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> fileOwnership = MakeRecordingOutput(&fileOutput);
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        ApplicationLogFanoutSink fanout{MakeConfig(), std::move(fileOwnership), std::move(consoleOwnership)};

        Submit(&fanout, MakeTransportPayload(100));
        Submit(&fanout, MakeTransportPayload(200));

        ASSERT_EQ(fileOutput->payloads.size(), 2);
        ASSERT_EQ(consoleOutput->payloads.size(), 2);
        const ApplicationLogJson firstFile = ApplicationLogJson::parse(fileOutput->payloads[0]);
        const ApplicationLogJson secondFile = ApplicationLogJson::parse(fileOutput->payloads[1]);

        EXPECT_EQ(firstFile.at("drainSequence").get<std::uint64_t>(), 1);
        EXPECT_EQ(secondFile.at("drainSequence").get<std::uint64_t>(), 2);
        EXPECT_EQ(firstFile.at("producerSteadyNs").get<std::uint64_t>(), 100);
        EXPECT_EQ(secondFile.at("producerSteadyNs").get<std::uint64_t>(), 200);
        EXPECT_NE(consoleOutput->payloads[0].find("component=world event=worker_stopped"), std::string::npos);
        EXPECT_EQ(fileOutput->payloads[0].find('\n'), std::string::npos);
        EXPECT_EQ(consoleOutput->payloads[0].find('\n'), std::string::npos);

        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_FALSE(snapshot.fileSinkFailed);
        EXPECT_FALSE(snapshot.consoleSinkFailed);
        EXPECT_EQ(snapshot.consumed, 2);
        EXPECT_EQ(snapshot.discardedAfterSinkFailure, 0);
    }

    TEST(ApplicationLogFanoutSinkTests, LatchesFileFailureAndContinuesConsoleWithoutRetry)
    {
        RecordingOutput* fileOutput = nullptr;
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> fileOwnership = MakeRecordingOutput(&fileOutput);
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        fileOutput->throwOnWrite = true;
        ApplicationLogFanoutSink fanout{MakeConfig(), std::move(fileOwnership), std::move(consoleOwnership)};

        Submit(&fanout, MakeTransportPayload(100));
        Submit(&fanout, MakeTransportPayload(200));

        EXPECT_EQ(fileOutput->writeCallCount, 1);
        EXPECT_EQ(consoleOutput->writeCallCount, 2);
        EXPECT_EQ(consoleOutput->payloads.size(), 2);

        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_TRUE(snapshot.fileSinkFailed);
        EXPECT_FALSE(snapshot.consoleSinkFailed);
        EXPECT_EQ(snapshot.consumed, 2);
        EXPECT_EQ(snapshot.discardedAfterSinkFailure, 0);
    }

    TEST(ApplicationLogFanoutSinkTests, LatchesConsoleFailureAndContinuesFileWithoutRetry)
    {
        RecordingOutput* fileOutput = nullptr;
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> fileOwnership = MakeRecordingOutput(&fileOutput);
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        consoleOutput->throwOnWrite = true;
        ApplicationLogFanoutSink fanout{MakeConfig(), std::move(fileOwnership), std::move(consoleOwnership)};

        Submit(&fanout, MakeTransportPayload(100));
        Submit(&fanout, MakeTransportPayload(200));

        EXPECT_EQ(fileOutput->writeCallCount, 2);
        EXPECT_EQ(fileOutput->payloads.size(), 2);
        EXPECT_EQ(consoleOutput->writeCallCount, 1);

        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_FALSE(snapshot.fileSinkFailed);
        EXPECT_TRUE(snapshot.consoleSinkFailed);
        EXPECT_EQ(snapshot.consumed, 2);
        EXPECT_EQ(snapshot.discardedAfterSinkFailure, 0);
    }

    TEST(ApplicationLogFanoutSinkTests, DiscardsNewRecordsAfterBothOutputsHaveFailed)
    {
        RecordingOutput* fileOutput = nullptr;
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> fileOwnership = MakeRecordingOutput(&fileOutput);
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        fileOutput->throwOnWrite = true;
        consoleOutput->throwOnWrite = true;
        ApplicationLogFanoutSink fanout{MakeConfig(), std::move(fileOwnership), std::move(consoleOwnership)};

        Submit(&fanout, MakeTransportPayload(100));
        Submit(&fanout, MakeTransportPayload(200));

        EXPECT_EQ(fileOutput->writeCallCount, 1);
        EXPECT_EQ(consoleOutput->writeCallCount, 1);

        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_TRUE(snapshot.fileSinkFailed);
        EXPECT_TRUE(snapshot.consoleSinkFailed);
        EXPECT_EQ(snapshot.consumed, 1);
        EXPECT_EQ(snapshot.discardedAfterSinkFailure, 1);
    }

    TEST(ApplicationLogFanoutSinkTests, IsolatesFlushFailureAndDoesNotRetryFailedOutput)
    {
        RecordingOutput* fileOutput = nullptr;
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> fileOwnership = MakeRecordingOutput(&fileOutput);
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        fileOutput->throwOnFlush = true;
        ApplicationLogFanoutSink fanout{MakeConfig(), std::move(fileOwnership), std::move(consoleOwnership)};

        fanout.Flush();
        fanout.Flush();

        EXPECT_EQ(fileOutput->flushCallCount, 1);
        EXPECT_EQ(consoleOutput->flushCallCount, 2);
        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_TRUE(snapshot.fileSinkFailed);
        EXPECT_FALSE(snapshot.consoleSinkFailed);
    }

    TEST(ApplicationLogFanoutSinkTests, SupportsDegradedStartupWithOneOutput)
    {
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        ApplicationLogFanoutSink fanout{MakeConfig(), nullptr, std::move(consoleOwnership)};

        Submit(&fanout, MakeTransportPayload(100));

        EXPECT_EQ(consoleOutput->writeCallCount, 1);
        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_TRUE(snapshot.fileSinkFailed);
        EXPECT_FALSE(snapshot.consoleSinkFailed);
        EXPECT_EQ(snapshot.consumed, 1);
    }

    TEST(ApplicationLogFanoutSinkTests, RejectsInvalidConfigAndMissingOutputs)
    {
        std::unique_ptr<RecordingOutput> consoleOwnership = std::make_unique<RecordingOutput>();
        ApplicationLogConfig invalidConfig = MakeConfig();
        invalidConfig.runId.clear();

        EXPECT_THROW(ApplicationLogFanoutSink(invalidConfig, nullptr, std::move(consoleOwnership)),
                     std::invalid_argument);
        EXPECT_THROW(ApplicationLogFanoutSink(MakeConfig(), nullptr, nullptr), std::invalid_argument);
    }

    TEST(ApplicationLogFanoutSinkTests, CountsRejectedBackendPayloadAsConsumed)
    {
        RecordingOutput* consoleOutput = nullptr;
        std::unique_ptr<RecordingOutput> consoleOwnership = MakeRecordingOutput(&consoleOutput);
        ApplicationLogFanoutSink fanout{MakeConfig(), nullptr, std::move(consoleOwnership)};

        EXPECT_THROW(fanout.Consume("not-an-envelope"), std::invalid_argument);

        const ApplicationLogFanoutSnapshot snapshot = fanout.Snapshot();
        EXPECT_EQ(snapshot.consumed, 1);
        EXPECT_EQ(snapshot.discardedAfterSinkFailure, 0);
        EXPECT_EQ(consoleOutput->writeCallCount, 0);
    }
} // namespace psnr::logging::internal
