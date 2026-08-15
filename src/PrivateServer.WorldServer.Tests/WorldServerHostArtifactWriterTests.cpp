#include "pch.h"

#include "WorldServerHostArtifactWriter.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace psnr::world::host::tests
{
    namespace
    {
        class TemporaryArtifactDirectory final
        {
        public:
            TemporaryArtifactDirectory()
            {
                path_ = std::filesystem::temp_directory_path() /
                        ("private-server-artifact-writer-" + std::to_string(GetCurrentProcessId()));
                std::error_code error;
                static_cast<void>(std::filesystem::remove_all(path_, error));
                valid_ = std::filesystem::create_directories(path_ / "world", error) && !error;
                if (valid_)
                {
                    valid_ = std::filesystem::create_directories(path_ / "runtime", error) && !error;
                }
            }

            ~TemporaryArtifactDirectory()
            {
                std::error_code error;
                static_cast<void>(std::filesystem::remove_all(path_, error));
            }

            TemporaryArtifactDirectory(const TemporaryArtifactDirectory&) = delete;
            TemporaryArtifactDirectory& operator=(const TemporaryArtifactDirectory&) = delete;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return valid_;
            }

            [[nodiscard]] const std::filesystem::path& Path() const noexcept
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
            bool valid_ = false;
        };

        [[nodiscard]] std::unique_ptr<WorldServerHostArtifactWriteQueue> CreateQueue()
        {
            WorldResult<std::unique_ptr<WorldServerHostArtifactWriteQueue>> result =
                WorldServerHostArtifactWriteQueue::Create(2, 2);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] std::unique_ptr<WorldTickSampleBuffer> CreateSampleBuffer(const std::uint32_t roundId,
                                                                                const bool addDroppedSample)
        {
            WorldResult<std::unique_ptr<WorldTickSampleBuffer>> result = WorldTickSampleBuffer::Create(1);
            EXPECT_TRUE(result.Succeeded());
            if (result.Failed())
            {
                return nullptr;
            }

            std::unique_ptr<WorldTickSampleBuffer> samples = result.TakeValue();
            const WorldTickSample sample{roundId, 100, 100, roundId, WorldRoundPhase::Running, 1, 2, 3, 4};
            EXPECT_TRUE(samples->TryRecord(sample));
            if (addDroppedSample)
            {
                EXPECT_FALSE(samples->TryRecord(sample));
            }
            return samples;
        }

        [[nodiscard]] WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus>
        CreateWriter(WorldServerHostArtifactWriteQueue& queue, const TemporaryArtifactDirectory& directory)
        {
            return WorldServerHostArtifactWriter::Create(
                queue, 7, "Channel 7", "run-1", directory.Path() / "world" / "tick-samples.jsonl",
                directory.Path() / "runtime" / "samples.jsonl", directory.Path() / "runtime" / "report.json");
        }
    } // namespace

    TEST(WorldServerHostArtifactWriterTests, RejectsMissingIdentityOrAnyOutputPath)
    {
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue();
        ASSERT_NE(queue, nullptr);

        EXPECT_TRUE(WorldServerHostArtifactWriter::Create(*queue, 0, "Channel 7", "run-1", "tick.jsonl",
                                                         "runtime.jsonl", "report.json")
                        .Failed());
        EXPECT_TRUE(WorldServerHostArtifactWriter::Create(*queue, 7, {}, "run-1", "tick.jsonl", "runtime.jsonl",
                                                         "report.json")
                        .Failed());
        EXPECT_TRUE(WorldServerHostArtifactWriter::Create(*queue, 7, "Channel 7", {}, "tick.jsonl", "runtime.jsonl",
                                                         "report.json")
                        .Failed());
        EXPECT_TRUE(WorldServerHostArtifactWriter::Create(*queue, 7, "Channel 7", "run-1", {}, "runtime.jsonl",
                                                         "report.json")
                        .Failed());
        EXPECT_TRUE(WorldServerHostArtifactWriter::Create(*queue, 7, "Channel 7", "run-1", "tick.jsonl", {},
                                                         "report.json")
                        .Failed());
        EXPECT_TRUE(WorldServerHostArtifactWriter::Create(*queue, 7, "Channel 7", "run-1", "tick.jsonl",
                                                         "runtime.jsonl", {})
                        .Failed());
    }

    TEST(WorldServerHostArtifactWriterTests, OneWorkerDrainsTickAndRuntimeLanesAndWritesBothReports)
    {
        const TemporaryArtifactDirectory directory;
        ASSERT_TRUE(directory.IsValid());
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue();
        ASSERT_NE(queue, nullptr);
        WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus> writerResult =
            CreateWriter(*queue, directory);
        ASSERT_TRUE(writerResult.Succeeded());
        std::unique_ptr<WorldServerHostArtifactWriter> writer = writerResult.TakeValue();
        ASSERT_EQ(writer->Start(), WorldServerHostArtifactWriterStatus::Running);

        std::unique_ptr<WorldTickSampleBuffer> first = CreateSampleBuffer(11, true);
        std::unique_ptr<WorldTickSampleBuffer> second = CreateSampleBuffer(12, false);
        ASSERT_EQ(queue->TrySubmit(std::move(first), WorldTickSampleBatchCompleteness::Complete),
                  WorldTickSampleSinkResult::Succeeded);
        ASSERT_EQ(queue->TrySubmit(std::move(second), WorldTickSampleBatchCompleteness::Incomplete),
                  WorldTickSampleSinkResult::Succeeded);
        WorldServerHostRuntimeSample runtimeSample;
        runtimeSample.sequence = 7;
        runtimeSample.elapsedMilliseconds = 1000;
        runtimeSample.captureDurationNanoseconds = 55;
        runtimeSample.captureStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
        ASSERT_EQ(queue->TryPushRuntimeSample(std::move(runtimeSample)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);
        WorldServerHostRuntimeSample terminalRuntimeSample;
        terminalRuntimeSample.sequence = 8;
        terminalRuntimeSample.elapsedMilliseconds = 2000;
        terminalRuntimeSample.captureDurationNanoseconds = 77;
        terminalRuntimeSample.captureStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
        ASSERT_EQ(queue->TryPushRuntimeSample(std::move(terminalRuntimeSample)),
                  WorldServerHostArtifactWriteQueueResult::Succeeded);

        writer->CloseAndJoin(2, true);

        EXPECT_EQ(writer->Status(), WorldServerHostArtifactWriterStatus::Completed);
        EXPECT_EQ(writer->WrittenTickBatchCount(), 2u);
        EXPECT_EQ(writer->WrittenTickSampleCount(), 2u);
        EXPECT_EQ(writer->IncompleteTickBatchCount(), 1u);
        EXPECT_EQ(writer->DroppedTickSampleCount(), 1u);
        EXPECT_EQ(writer->WrittenRuntimeSampleCount(), 2u);

        std::ifstream tickInput(directory.Path() / "world" / "tick-samples.jsonl", std::ios::binary);
        ASSERT_TRUE(tickInput.is_open());
        std::vector<nlohmann::ordered_json> tickLines;
        std::string line;
        while (std::getline(tickInput, line))
        {
            tickLines.push_back(nlohmann::ordered_json::parse(line));
        }
        ASSERT_EQ(tickLines.size(), 2u);
        EXPECT_EQ(tickLines[0]["roundId"], 11);
        EXPECT_EQ(tickLines[1]["roundId"], 12);

        std::ifstream runtimeInput(directory.Path() / "runtime" / "samples.jsonl", std::ios::binary);
        ASSERT_TRUE(runtimeInput.is_open());
        std::vector<nlohmann::ordered_json> runtimeLines;
        while (std::getline(runtimeInput, line))
        {
            runtimeLines.push_back(nlohmann::ordered_json::parse(line));
        }
        ASSERT_EQ(runtimeLines.size(), 2u);
        EXPECT_EQ(runtimeLines[0]["schema"], "psnr.runtime.sample");
        EXPECT_EQ(runtimeLines[0]["sequence"], 7);
        EXPECT_EQ(runtimeLines[0]["elapsedMilliseconds"], 1000);
        EXPECT_EQ(runtimeLines[0]["captureDurationNanoseconds"], 55);
        EXPECT_EQ(runtimeLines[0]["snapshotCaptureSucceeded"], false);
        EXPECT_EQ(runtimeLines[0]["sendMailboxDepth"], 0);
        EXPECT_EQ(runtimeLines[0]["pendingSendQueueDepth"], 0);
        EXPECT_EQ(runtimeLines[1]["sequence"], 8);
        EXPECT_EQ(runtimeLines[1]["elapsedMilliseconds"], 2000);
        EXPECT_EQ(runtimeLines[1]["captureDurationNanoseconds"], 77);

        std::ifstream worldReportInput(directory.Path() / "world" / "report.json", std::ios::binary);
        ASSERT_TRUE(worldReportInput.is_open());
        const nlohmann::ordered_json worldReport = nlohmann::ordered_json::parse(worldReportInput);
        EXPECT_EQ(worldReport["version"], 2);
        EXPECT_EQ(worldReport["channelId"], 7);
        EXPECT_EQ(worldReport["channelName"], "Channel 7");
        EXPECT_EQ(worldReport["status"], "incomplete");
        EXPECT_EQ(worldReport["observedBatchCount"], 2);
        EXPECT_EQ(worldReport["collectionFailureCount"], 2);

        std::ifstream runtimeReportInput(directory.Path() / "runtime" / "report.json", std::ios::binary);
        ASSERT_TRUE(runtimeReportInput.is_open());
        const nlohmann::ordered_json runtimeReport = nlohmann::ordered_json::parse(runtimeReportInput);
        EXPECT_EQ(runtimeReport["schema"], "psnr.runtime.report");
        EXPECT_EQ(runtimeReport["status"], "incomplete");
        EXPECT_FALSE(runtimeReport.contains("channelId"));
    }

    TEST(WorldServerHostArtifactWriterTests, WritesIncompleteReportsWhenClosedWithoutSamples)
    {
        const TemporaryArtifactDirectory directory;
        ASSERT_TRUE(directory.IsValid());
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue();
        ASSERT_NE(queue, nullptr);
        WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus> writerResult =
            CreateWriter(*queue, directory);
        ASSERT_TRUE(writerResult.Succeeded());
        std::unique_ptr<WorldServerHostArtifactWriter> writer = writerResult.TakeValue();
        ASSERT_EQ(writer->Start(), WorldServerHostArtifactWriterStatus::Running);

        writer->CloseAndJoin(0, true);

        EXPECT_EQ(writer->Status(), WorldServerHostArtifactWriterStatus::Completed);
        std::ifstream worldReportInput(directory.Path() / "world" / "report.json", std::ios::binary);
        std::ifstream runtimeReportInput(directory.Path() / "runtime" / "report.json", std::ios::binary);
        ASSERT_TRUE(worldReportInput.is_open());
        ASSERT_TRUE(runtimeReportInput.is_open());
        EXPECT_EQ(nlohmann::ordered_json::parse(worldReportInput)["status"], "incomplete");
        EXPECT_EQ(nlohmann::ordered_json::parse(runtimeReportInput)["status"], "incomplete");
    }

    TEST(WorldServerHostArtifactWriterTests, ReportsOpenFailureBeforeStartingWorkerThread)
    {
        const TemporaryArtifactDirectory directory;
        ASSERT_TRUE(directory.IsValid());
        std::unique_ptr<WorldServerHostArtifactWriteQueue> queue = CreateQueue();
        ASSERT_NE(queue, nullptr);
        WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus> writerResult =
            WorldServerHostArtifactWriter::Create(*queue, 7, "Channel 7", "run-1",
                                                  directory.Path() / "missing" / "tick-samples.jsonl",
                                                  directory.Path() / "runtime" / "samples.jsonl",
                                                  directory.Path() / "runtime" / "report.json");
        ASSERT_TRUE(writerResult.Succeeded());
        std::unique_ptr<WorldServerHostArtifactWriter> writer = writerResult.TakeValue();

        EXPECT_EQ(writer->Start(), WorldServerHostArtifactWriterStatus::OpenFailed);
        EXPECT_EQ(writer->Status(), WorldServerHostArtifactWriterStatus::OpenFailed);
    }
} // namespace psnr::world::host::tests
