#include "pch.h"

#include "WorldServerHostArtifactWriter.h"

#include "WorldServerHostRuntimeArtifactWriter.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>

#include <nlohmann/json.hpp>

#include <exception>
#include <new>
#include <span>
#include <system_error>
#include <utility>

namespace psnr::world::host
{
    namespace
    {
        constexpr std::string_view TickSampleSchema = "psnr.world.tick_sample";
        constexpr std::uint32_t TickSampleSchemaVersion = 1;
        constexpr std::string_view WorldReportSchema = "psnr.world.report";
        constexpr std::uint32_t WorldReportSchemaVersion = 2;
        constexpr std::string_view WorldReportFileName = "report.json";
        constexpr std::string_view TemporarySuffix = ".tmp";

        [[nodiscard]] std::string_view CompletenessName(const WorldTickSampleBatchCompleteness completeness) noexcept
        {
            return completeness == WorldTickSampleBatchCompleteness::Complete ? "complete" : "incomplete";
        }

        [[nodiscard]] std::string_view RoundPhaseName(const WorldRoundPhase phase) noexcept
        {
            switch (phase)
            {
            case WorldRoundPhase::Waiting:
                return "waiting";
            case WorldRoundPhase::Running:
                return "running";
            case WorldRoundPhase::Ended:
                return "ended";
            default:
                return "invalid";
            }
        }
    } // namespace

    WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus>
    WorldServerHostArtifactWriter::Create(WorldServerHostArtifactWriteQueue& writeQueue,
                                          const std::uint32_t channelId, const std::string_view channelName,
                                          const std::string_view runId,
                                          const std::filesystem::path& tickSampleOutputPath,
                                          const std::filesystem::path& runtimeSampleOutputPath,
                                          const std::filesystem::path& runtimeReportPath) noexcept
    {
        if (channelId == 0 || channelName.empty() || runId.empty() || tickSampleOutputPath.empty() ||
            runtimeSampleOutputPath.empty() ||
            runtimeReportPath.empty())
        {
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus>::
                Failure(WorldServerHostArtifactWriterStatus::InvalidArgument);
        }

        try
        {
            std::unique_ptr<WorldServerHostArtifactWriter> writer{new WorldServerHostArtifactWriter(
                writeQueue, channelId, std::string{channelName}, std::string{runId},
                std::filesystem::path{tickSampleOutputPath},
                std::filesystem::path{runtimeSampleOutputPath}, std::filesystem::path{runtimeReportPath})};
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus>{
                std::move(writer)};
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>, WorldServerHostArtifactWriterStatus>::
                Failure(WorldServerHostArtifactWriterStatus::AllocationFailed);
        }
    }

    WorldServerHostArtifactWriter::WorldServerHostArtifactWriter(WorldServerHostArtifactWriteQueue& writeQueue,
                                                                 const std::uint32_t channelId,
                                                                 std::string channelName, std::string runId,
                                                                 std::filesystem::path tickSampleOutputPath,
                                                                 std::filesystem::path runtimeSampleOutputPath,
                                                                 std::filesystem::path runtimeReportPath) noexcept
        : writeQueue_(writeQueue)
        , channelId_(channelId)
        , channelName_(std::move(channelName))
        , runId_(std::move(runId))
        , tickSampleOutputPath_(std::move(tickSampleOutputPath))
        , worldReportPath_(tickSampleOutputPath_.parent_path() / WorldReportFileName)
        , runtimeSampleOutputPath_(std::move(runtimeSampleOutputPath))
        , runtimeReportPath_(std::move(runtimeReportPath))
    {
    }

    WorldServerHostArtifactWriter::~WorldServerHostArtifactWriter()
    {
        CloseAndJoin();
    }

    WorldServerHostArtifactWriterStatus WorldServerHostArtifactWriter::Start() noexcept
    {
        if (status_.load(std::memory_order_acquire) != WorldServerHostArtifactWriterStatus::NotStarted)
        {
            return WorldServerHostArtifactWriterStatus::InvalidArgument;
        }

        tickSampleOutput_.open(tickSampleOutputPath_, std::ios::binary | std::ios::trunc);
        runtimeSampleOutput_.open(runtimeSampleOutputPath_, std::ios::binary | std::ios::trunc);
        if (!tickSampleOutput_.is_open() || !runtimeSampleOutput_.is_open())
        {
            tickSampleOutput_.close();
            runtimeSampleOutput_.close();
            status_.store(WorldServerHostArtifactWriterStatus::OpenFailed, std::memory_order_release);
            return WorldServerHostArtifactWriterStatus::OpenFailed;
        }

        status_.store(WorldServerHostArtifactWriterStatus::Running, std::memory_order_release);
        try
        {
            thread_ = std::thread{&WorldServerHostArtifactWriter::Run, this};
            started_ = true;
        }
        catch (const std::system_error&)
        {
            tickSampleOutput_.close();
            runtimeSampleOutput_.close();
            status_.store(WorldServerHostArtifactWriterStatus::ThreadStartFailed, std::memory_order_release);
            return WorldServerHostArtifactWriterStatus::ThreadStartFailed;
        }
        return WorldServerHostArtifactWriterStatus::Running;
    }

    void WorldServerHostArtifactWriter::CloseAndJoin(const std::uint64_t collectionFailureCount,
                                                     const bool worldShutdownCompleted) noexcept
    {
        if (!started_)
        {
            return;
        }

        collectionFailureCount_ = collectionFailureCount;
        worldShutdownCompleted_ = worldShutdownCompleted;
        writeQueue_.Close();
        if (thread_.joinable())
        {
            thread_.join();
        }
        started_ = false;
    }

    WorldServerHostArtifactWriterStatus WorldServerHostArtifactWriter::Status() const noexcept
    {
        return status_.load(std::memory_order_acquire);
    }

    std::uint64_t WorldServerHostArtifactWriter::WrittenTickBatchCount() const noexcept
    {
        return writtenTickBatchCount_.load(std::memory_order_acquire);
    }

    std::uint64_t WorldServerHostArtifactWriter::WrittenTickSampleCount() const noexcept
    {
        return writtenTickSampleCount_.load(std::memory_order_acquire);
    }

    std::uint64_t WorldServerHostArtifactWriter::IncompleteTickBatchCount() const noexcept
    {
        return incompleteTickBatchCount_.load(std::memory_order_acquire);
    }

    std::uint64_t WorldServerHostArtifactWriter::DroppedTickSampleCount() const noexcept
    {
        return droppedTickSampleCount_.load(std::memory_order_acquire);
    }

    std::uint64_t WorldServerHostArtifactWriter::WrittenRuntimeSampleCount() const noexcept
    {
        return writtenRuntimeSampleCount_.load(std::memory_order_acquire);
    }

    void WorldServerHostArtifactWriter::Run() noexcept
    {
        bool tickWriteSucceeded = true;
        bool runtimeWriteSucceeded = true;
        for (;;)
        {
            const WorldServerHostArtifactWriteQueueResult waitResult = writeQueue_.WaitForWork();
            if (waitResult == WorldServerHostArtifactWriteQueueResult::Closed)
            {
                break;
            }
            if (waitResult != WorldServerHostArtifactWriteQueueResult::Succeeded)
            {
                tickWriteSucceeded = false;
                runtimeWriteSucceeded = false;
                break;
            }

            // Runtime sample을 먼저 처리해 1초 시계열의 지연을 줄인다. 그 뒤 tick batch도
            // 최대 하나 처리하므로 Runtime sample이 계속 들어와도 tick lane이 굶지 않는다.
            if (!ConsumeRuntimeSample())
            {
                runtimeWriteSucceeded = false;
            }
            if (!ConsumeTickBatch())
            {
                tickWriteSucceeded = false;
            }
        }

        tickSampleOutput_.close();
        runtimeSampleOutput_.close();
        const bool worldReportWriteSucceeded = WriteWorldReport(tickWriteSucceeded);
        const bool runtimeReportWriteSucceeded = WriteRuntimeReport();
        const bool succeeded =
            tickWriteSucceeded && runtimeWriteSucceeded && worldReportWriteSucceeded && runtimeReportWriteSucceeded;
        status_.store(succeeded ? WorldServerHostArtifactWriterStatus::Completed
                                : WorldServerHostArtifactWriterStatus::WriteFailed,
                      std::memory_order_release);
    }

    bool WorldServerHostArtifactWriter::ConsumeRuntimeSample() noexcept
    {
        WorldServerHostRuntimeSample sample;
        const WorldServerHostArtifactWriteQueueResult popResult = writeQueue_.TryPopRuntimeSample(&sample);
        if (popResult == WorldServerHostArtifactWriteQueueResult::Empty ||
            popResult == WorldServerHostArtifactWriteQueueResult::Closed)
        {
            return true;
        }
        if (popResult != WorldServerHostArtifactWriteQueueResult::Succeeded)
        {
            return false;
        }

        const WorldResult<void, WorldServerHostRuntimeArtifactWriteError> writeResult =
            WorldServerHostRuntimeArtifactWriter::WriteSampleLine(runtimeSampleOutput_, runId_, sample);
        latestRuntimeSample_ = std::move(sample);
        hasRuntimeSample_ = true;
        if (writeResult.Failed())
        {
            return false;
        }
        writtenRuntimeSampleCount_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool WorldServerHostArtifactWriter::ConsumeTickBatch() noexcept
    {
        WorldTickSampleBatch batch;
        const WorldServerHostArtifactWriteQueueResult popResult = writeQueue_.TryPopTickBatch(&batch);
        if (popResult == WorldServerHostArtifactWriteQueueResult::Empty ||
            popResult == WorldServerHostArtifactWriteQueueResult::Closed)
        {
            return true;
        }
        if (popResult != WorldServerHostArtifactWriteQueueResult::Succeeded)
        {
            return false;
        }

        observedTickBatchCount_.fetch_add(1, std::memory_order_relaxed);
        if (batch.completeness == WorldTickSampleBatchCompleteness::Incomplete)
        {
            incompleteTickBatchCount_.fetch_add(1, std::memory_order_relaxed);
        }
        droppedTickSampleCount_.fetch_add(batch.samples->DroppedSampleCount(), std::memory_order_relaxed);
        return WriteTickBatch(batch);
    }

    bool WorldServerHostArtifactWriter::WriteTickBatch(const WorldTickSampleBatch& batch) noexcept
    {
        try
        {
            const std::span<const WorldTickSample> samples = batch.samples->Samples();
            for (const WorldTickSample& sample : samples)
            {
                nlohmann::ordered_json document;
                document["schema"] = TickSampleSchema;
                document["version"] = TickSampleSchemaVersion;
                document["runId"] = runId_;
                document["batchCompleteness"] = CompletenessName(batch.completeness);
                document["epoch"] = sample.epoch;
                document["firstServerTick"] = sample.firstServerTick;
                document["lastServerTick"] = sample.lastServerTick;
                document["roundId"] = sample.roundId;
                document["roundPhase"] = RoundPhaseName(sample.roundPhase);
                document["processedTickCount"] = sample.processedTickCount;
                document["dueTickCount"] = sample.dueTickCount;
                document["startLagNanoseconds"] = sample.startLagNanoseconds;
                document["executionDurationNanoseconds"] = sample.executionDurationNanoseconds;
                tickSampleOutput_ << document.dump() << '\n';
                if (!tickSampleOutput_.good())
                {
                    return false;
                }
                writtenTickSampleCount_.fetch_add(1, std::memory_order_relaxed);
            }
            tickSampleOutput_.flush();
            if (!tickSampleOutput_.good())
            {
                return false;
            }
            writtenTickBatchCount_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool WorldServerHostArtifactWriter::WriteWorldReport(const bool tickSampleWriteSucceeded) noexcept
    {
        const std::filesystem::path temporaryPath =
            std::filesystem::path{worldReportPath_.generic_string() + std::string{TemporarySuffix}};
        try
        {
            const std::uint64_t observedBatchCount = observedTickBatchCount_.load(std::memory_order_acquire);
            const std::uint64_t writtenBatchCount = writtenTickBatchCount_.load(std::memory_order_acquire);
            const std::uint64_t writtenSampleCount = writtenTickSampleCount_.load(std::memory_order_acquire);
            const std::uint64_t incompleteBatchCount = incompleteTickBatchCount_.load(std::memory_order_acquire);
            const std::uint64_t droppedSampleCount = droppedTickSampleCount_.load(std::memory_order_acquire);
            const std::uint64_t completeBatchCount = observedBatchCount - incompleteBatchCount;
            const bool artifactComplete = tickSampleWriteSucceeded && worldShutdownCompleted_ &&
                                          completeBatchCount > 0 && incompleteBatchCount == 0 &&
                                          droppedSampleCount == 0 && collectionFailureCount_ == 0;

            nlohmann::ordered_json document;
            document["schema"] = WorldReportSchema;
            document["version"] = WorldReportSchemaVersion;
            document["runId"] = runId_;
            document["channelId"] = channelId_;
            document["channelName"] = channelName_;
            document["status"] = artifactComplete ? "complete" : "incomplete";
            document["worldShutdownCompleted"] = worldShutdownCompleted_;
            document["tickSampleWriteSucceeded"] = tickSampleWriteSucceeded;
            document["observedBatchCount"] = observedBatchCount;
            document["writtenBatchCount"] = writtenBatchCount;
            document["completeBatchCount"] = completeBatchCount;
            document["incompleteBatchCount"] = incompleteBatchCount;
            document["writtenSampleCount"] = writtenSampleCount;
            document["droppedSampleCount"] = droppedSampleCount;
            document["collectionFailureCount"] = collectionFailureCount_;

            std::ofstream reportOutput(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!reportOutput.is_open())
            {
                return false;
            }
            reportOutput << document.dump(2) << '\n';
            reportOutput.flush();
            if (!reportOutput.good())
            {
                reportOutput.close();
                std::error_code removeError;
                static_cast<void>(std::filesystem::remove(temporaryPath, removeError));
                return false;
            }
            reportOutput.close();

            std::error_code renameError;
            std::filesystem::rename(temporaryPath, worldReportPath_, renameError);
            if (renameError)
            {
                std::error_code removeError;
                static_cast<void>(std::filesystem::remove(temporaryPath, removeError));
                return false;
            }
            return true;
        }
        catch (const std::exception&)
        {
            std::error_code removeError;
            static_cast<void>(std::filesystem::remove(temporaryPath, removeError));
            return false;
        }
    }

    bool WorldServerHostArtifactWriter::WriteRuntimeReport() noexcept
    {
        if (!hasRuntimeSample_)
        {
            const psnr::core::NrStatus captureStatus =
                psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
            const psnr::runtime::NrServerSnapshot snapshot;
            return WorldServerHostRuntimeArtifactWriter::Write(runId_, runtimeReportPath_, captureStatus, snapshot)
                .Succeeded();
        }
        return WorldServerHostRuntimeArtifactWriter::Write(
                   runId_, runtimeReportPath_, latestRuntimeSample_.captureStatus, latestRuntimeSample_.snapshot)
            .Succeeded();
    }
} // namespace psnr::world::host
