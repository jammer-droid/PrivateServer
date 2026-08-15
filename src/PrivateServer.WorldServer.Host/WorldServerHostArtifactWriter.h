#pragma once

#include "WorldResult.h"
#include "WorldServerHostArtifactWriteQueue.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace psnr::world::host
{
    enum class WorldServerHostArtifactWriterStatus : std::uint8_t
    {
        NotStarted = 0,
        Running,
        Completed,
        InvalidArgument,
        AllocationFailed,
        OpenFailed,
        ThreadStartFailed,
        WriteFailed,
    };

    // 이 worker 하나가 World round batch와 Runtime snapshot을 모두 기록한다.
    // queue의 두 lane을 번갈아 소비하되 파일별 stream과 report 상태는 서로 분리한다.
    class WorldServerHostArtifactWriter final
    {
    public:
        [[nodiscard]] static WorldResult<std::unique_ptr<WorldServerHostArtifactWriter>,
                                         WorldServerHostArtifactWriterStatus>
        Create(WorldServerHostArtifactWriteQueue& writeQueue, std::uint32_t channelId, std::string_view channelName,
               std::string_view runId,
               const std::filesystem::path& tickSampleOutputPath, const std::filesystem::path& runtimeSampleOutputPath,
               const std::filesystem::path& runtimeReportPath) noexcept;

        ~WorldServerHostArtifactWriter();

        WorldServerHostArtifactWriter(const WorldServerHostArtifactWriter&) = delete;
        WorldServerHostArtifactWriter& operator=(const WorldServerHostArtifactWriter&) = delete;

        [[nodiscard]] WorldServerHostArtifactWriterStatus Start() noexcept;
        void CloseAndJoin(std::uint64_t collectionFailureCount = 0, bool worldShutdownCompleted = false) noexcept;

        [[nodiscard]] WorldServerHostArtifactWriterStatus Status() const noexcept;
        [[nodiscard]] std::uint64_t WrittenTickBatchCount() const noexcept;
        [[nodiscard]] std::uint64_t WrittenTickSampleCount() const noexcept;
        [[nodiscard]] std::uint64_t IncompleteTickBatchCount() const noexcept;
        [[nodiscard]] std::uint64_t DroppedTickSampleCount() const noexcept;
        [[nodiscard]] std::uint64_t WrittenRuntimeSampleCount() const noexcept;

    private:
        WorldServerHostArtifactWriter(WorldServerHostArtifactWriteQueue& writeQueue, std::uint32_t channelId,
                                      std::string channelName, std::string runId,
                                      std::filesystem::path tickSampleOutputPath,
                                      std::filesystem::path runtimeSampleOutputPath,
                                      std::filesystem::path runtimeReportPath) noexcept;

        void Run() noexcept;
        [[nodiscard]] bool ConsumeRuntimeSample() noexcept;
        [[nodiscard]] bool ConsumeTickBatch() noexcept;
        [[nodiscard]] bool WriteTickBatch(const WorldTickSampleBatch& batch) noexcept;
        [[nodiscard]] bool WriteWorldReport(bool tickSampleWriteSucceeded) noexcept;
        [[nodiscard]] bool WriteRuntimeReport() noexcept;

        WorldServerHostArtifactWriteQueue& writeQueue_;
        std::uint32_t channelId_ = 0;
        std::string channelName_;
        std::string runId_;
        std::filesystem::path tickSampleOutputPath_;
        std::filesystem::path worldReportPath_;
        std::filesystem::path runtimeSampleOutputPath_;
        std::filesystem::path runtimeReportPath_;
        std::ofstream tickSampleOutput_;
        std::ofstream runtimeSampleOutput_;
        std::thread thread_;
        std::atomic<WorldServerHostArtifactWriterStatus> status_ = WorldServerHostArtifactWriterStatus::NotStarted;
        std::atomic<std::uint64_t> observedTickBatchCount_ = 0;
        std::atomic<std::uint64_t> writtenTickBatchCount_ = 0;
        std::atomic<std::uint64_t> writtenTickSampleCount_ = 0;
        std::atomic<std::uint64_t> incompleteTickBatchCount_ = 0;
        std::atomic<std::uint64_t> droppedTickSampleCount_ = 0;
        std::atomic<std::uint64_t> writtenRuntimeSampleCount_ = 0;
        WorldServerHostRuntimeSample latestRuntimeSample_{};
        std::uint64_t collectionFailureCount_ = 0;
        bool hasRuntimeSample_ = false;
        bool worldShutdownCompleted_ = false;
        bool started_ = false;
    };
} // namespace psnr::world::host
