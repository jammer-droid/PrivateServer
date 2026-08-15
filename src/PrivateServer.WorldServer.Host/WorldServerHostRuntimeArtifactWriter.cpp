#include "pch.h"

#include "WorldServerHostRuntimeArtifactWriter.h"

#include <nlohmann/json.hpp>

#include <array>
#include <exception>
#include <fstream>
#include <string>
#include <system_error>

namespace psnr::world::host
{
    namespace
    {
        constexpr std::string_view RuntimeReportSchema = "psnr.runtime.report";
        constexpr std::uint32_t RuntimeReportSchemaVersion = 1;
        constexpr std::string_view RuntimeSampleSchema = "psnr.runtime.sample";
        constexpr std::uint32_t RuntimeSampleSchemaVersion = 1;
        constexpr std::string_view TemporarySuffix = ".tmp";

        struct PressureOutcomeName final
        {
            psnr::runtime::NrPressureTransactionOutcome outcome;
            std::string_view name;
        };

        struct MemoryPoolRoleName final
        {
            psnr::runtime::NrServerMemoryPoolRole role;
            std::string_view name;
        };

        constexpr std::array<PressureOutcomeName, psnr::runtime::NrPressureTransactionOutcomeCount>
            PressureOutcomeNames{{
                {psnr::runtime::NrPressureTransactionOutcome::ToWorldPacketAdmissionRejected,
                 "toWorldPacketAdmissionRejected"},
                {psnr::runtime::NrPressureTransactionOutcome::ToWorldLifecyclePublicationDeferred,
                 "toWorldLifecyclePublicationDeferred"},
                {psnr::runtime::NrPressureTransactionOutcome::ActorAdmissionMailboxRejected,
                 "actorAdmissionMailboxRejected"},
                {psnr::runtime::NrPressureTransactionOutcome::ActorAdmissionReadyCapacityRejected,
                 "actorAdmissionReadyCapacityRejected"},
                {psnr::runtime::NrPressureTransactionOutcome::SendAdmissionRejected, "sendAdmissionRejected"},
                {psnr::runtime::NrPressureTransactionOutcome::ReceivePressureCloseCommitted,
                 "receivePressureCloseCommitted"},
                {psnr::runtime::NrPressureTransactionOutcome::SendPressureCloseCommitted, "sendPressureCloseCommitted"},
                {psnr::runtime::NrPressureTransactionOutcome::SendResourceAcquireFailed, "sendResourceAcquireFailed"},
                {psnr::runtime::NrPressureTransactionOutcome::NativeSendPostFailed, "nativeSendPostFailed"},
            }};

        constexpr std::array<MemoryPoolRoleName, psnr::runtime::NrServerMemoryPoolRoleCount> MemoryPoolRoleNames{{
            {psnr::runtime::NrServerMemoryPoolRole::RecvBuffer, "recvBuffer"},
            {psnr::runtime::NrServerMemoryPoolRole::OverlappedContext, "overlappedContext"},
            {psnr::runtime::NrServerMemoryPoolRole::RuntimeIngressQueueStorage, "runtimeIngressQueueStorage"},
            {psnr::runtime::NrServerMemoryPoolRole::ToWorldEventQueueStorage, "toWorldEventQueueStorage"},
            {psnr::runtime::NrServerMemoryPoolRole::SessionAcceptRecvMailboxStorage, "sessionAcceptRecvMailboxStorage"},
            {psnr::runtime::NrServerMemoryPoolRole::SessionSendMailboxStorage, "sessionSendMailboxStorage"},
            {psnr::runtime::NrServerMemoryPoolRole::ActorReadyQueueStorage, "actorReadyQueueStorage"},
            {psnr::runtime::NrServerMemoryPoolRole::Payload64, "payload64"},
            {psnr::runtime::NrServerMemoryPoolRole::Payload256, "payload256"},
            {psnr::runtime::NrServerMemoryPoolRole::Payload1024, "payload1024"},
            {psnr::runtime::NrServerMemoryPoolRole::Payload8192, "payload8192"},
            {psnr::runtime::NrServerMemoryPoolRole::PayloadRefControl, "payloadRefControl"},
        }};

        [[nodiscard]] std::string_view LifecycleStateName(const psnr::runtime::NrServerLifecycleState state) noexcept
        {
            switch (state)
            {
            case psnr::runtime::NrServerLifecycleState::Created:
                return "created";
            case psnr::runtime::NrServerLifecycleState::Running:
                return "running";
            case psnr::runtime::NrServerLifecycleState::StopRequested:
                return "stop_requested";
            case psnr::runtime::NrServerLifecycleState::Shutdown:
                return "shutdown";
            default:
                return "invalid";
            }
        }

        [[nodiscard]] bool IsComplete(const psnr::core::NrStatus& captureStatus,
                                      const psnr::runtime::NrServerSnapshot& snapshot) noexcept
        {
            if (captureStatus.Failed() || !snapshot.IsValid() ||
                snapshot.LifecycleState() != psnr::runtime::NrServerLifecycleState::Shutdown ||
                snapshot.RegisteredSessionCount() != 0 || snapshot.ClosingSessionCount() != 0 ||
                snapshot.PendingIoCount() != 0 || snapshot.ToWorldEventDepth() != 0)
            {
                return false;
            }

            const psnr::runtime::NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
            return diagnostics.enabled && !diagnostics.sinkFailed && diagnostics.droppedQueueFull == 0 &&
                   diagnostics.droppedSinkUnavailable == 0 && diagnostics.discardedAfterSinkFailure == 0 &&
                   diagnostics.attempted == diagnostics.enqueued && diagnostics.enqueued == diagnostics.consumed;
        }

        void AppendSnapshotFields(nlohmann::ordered_json& document, const psnr::runtime::NrServerSnapshot& snapshot)
        {
            const psnr::runtime::NrServerDiagnosticsSnapshot diagnostics = snapshot.Diagnostics();
            document["snapshotValid"] = snapshot.IsValid();
            document["lifecycleState"] = LifecycleStateName(snapshot.LifecycleState());
            document["registeredSessionCount"] = snapshot.RegisteredSessionCount();
            document["closingSessionCount"] = snapshot.ClosingSessionCount();
            document["pendingRecvIoCount"] = snapshot.PendingRecvIoCount();
            document["pendingSendIoCount"] = snapshot.PendingSendIoCount();
            document["pendingIoCount"] = snapshot.PendingIoCount();
            document["sendMailboxDepth"] = snapshot.SendMailboxDepth();
            document["sendMailboxHighWatermark"] = snapshot.SendMailboxHighWatermark();
            document["pendingSendQueueDepth"] = snapshot.PendingSendQueueDepth();
            document["pendingSendQueueHighWatermark"] = snapshot.PendingSendQueueHighWatermark();
            document["toWorldEventDepth"] = snapshot.ToWorldEventDepth();
            document["toWorldEventHighWatermark"] = snapshot.ToWorldEventHighWatermark();
            document["totalPressureTransactions"] = snapshot.TotalPressureTransactions();

            nlohmann::ordered_json pressureTransactions = nlohmann::ordered_json::object();
            for (const PressureOutcomeName& entry : PressureOutcomeNames)
            {
                pressureTransactions[entry.name] = snapshot.PressureTransactionCount(entry.outcome);
            }
            document["pressureTransactions"] = std::move(pressureTransactions);

            nlohmann::ordered_json memoryPools = nlohmann::ordered_json::object();
            nlohmann::ordered_json poolPressure = nlohmann::ordered_json::object();
            for (const MemoryPoolRoleName& entry : MemoryPoolRoleNames)
            {
                const psnr::runtime::NrServerMemoryPoolSnapshot pool = snapshot.MemoryPool(entry.role);
                memoryPools[entry.name] = {
                    {"capacity", pool.capacity},
                    {"inUse", pool.inUse},
                    {"available", pool.available},
                    {"highWatermark", pool.highWatermark},
                };
                poolPressure[entry.name] = {
                    {"exhausted",
                     snapshot.PoolAcquirePressureCount(entry.role, psnr::runtime::NrPoolPressureOutcome::Exhausted)},
                };
            }
            document["memoryPools"] = std::move(memoryPools);
            document["poolPressure"] = std::move(poolPressure);
            document["diagnostics"] = {
                {"enabled", diagnostics.enabled},
                {"sinkFailed", diagnostics.sinkFailed},
                {"attempted", diagnostics.attempted},
                {"enqueued", diagnostics.enqueued},
                {"droppedQueueFull", diagnostics.droppedQueueFull},
                {"droppedSinkUnavailable", diagnostics.droppedSinkUnavailable},
                {"consumed", diagnostics.consumed},
                {"discardedAfterSinkFailure", diagnostics.discardedAfterSinkFailure},
            };
        }
    } // namespace

    WorldResult<void, WorldServerHostRuntimeArtifactWriteError> WorldServerHostRuntimeArtifactWriter::WriteSampleLine(
        std::ostream& output, const std::string_view runId, const WorldServerHostRuntimeSample& sample) noexcept
    {
        if (runId.empty())
        {
            return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                WorldServerHostRuntimeArtifactWriteError::InvalidArgument);
        }

        try
        {
            nlohmann::ordered_json document;
            document["schema"] = RuntimeSampleSchema;
            document["version"] = RuntimeSampleSchemaVersion;
            document["runId"] = runId;
            document["sequence"] = sample.sequence;
            document["elapsedMilliseconds"] = sample.elapsedMilliseconds;
            document["captureDurationNanoseconds"] = sample.captureDurationNanoseconds;
            document["snapshotCaptureSucceeded"] = sample.captureStatus.Succeeded();
            document["snapshotCaptureErrorCode"] = static_cast<std::uint32_t>(sample.captureStatus.ErrorCode());
            document["snapshotCaptureNativeErrorCode"] = sample.captureStatus.NativeErrorCode();
            AppendSnapshotFields(document, sample.snapshot);
            output << document.dump() << '\n';
            output.flush();
            if (!output.good())
            {
                return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                    WorldServerHostRuntimeArtifactWriteError::WriteFailed);
            }
            return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Success();
        }
        catch (const std::exception&)
        {
            return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                WorldServerHostRuntimeArtifactWriteError::WriteFailed);
        }
    }

    WorldResult<void, WorldServerHostRuntimeArtifactWriteError> WorldServerHostRuntimeArtifactWriter::Write(
        const std::string_view runId, const std::filesystem::path& outputPath,
        const psnr::core::NrStatus& snapshotCaptureStatus, const psnr::runtime::NrServerSnapshot& snapshot) noexcept
    {
        if (runId.empty() || outputPath.empty())
        {
            return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                WorldServerHostRuntimeArtifactWriteError::InvalidArgument);
        }

        const std::filesystem::path temporaryPath =
            std::filesystem::path{outputPath.generic_string() + std::string{TemporarySuffix}};
        try
        {
            nlohmann::ordered_json document;
            document["schema"] = RuntimeReportSchema;
            document["version"] = RuntimeReportSchemaVersion;
            document["runId"] = runId;
            document["status"] = IsComplete(snapshotCaptureStatus, snapshot) ? "complete" : "incomplete";
            document["snapshotCaptureSucceeded"] = snapshotCaptureStatus.Succeeded();
            document["snapshotCaptureErrorCode"] = static_cast<std::uint32_t>(snapshotCaptureStatus.ErrorCode());
            document["snapshotCaptureNativeErrorCode"] = snapshotCaptureStatus.NativeErrorCode();
            AppendSnapshotFields(document, snapshot);

            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                    WorldServerHostRuntimeArtifactWriteError::WriteFailed);
            }
            output << document.dump(2) << '\n';
            output.flush();
            if (!output.good())
            {
                output.close();
                std::error_code removeError;
                static_cast<void>(std::filesystem::remove(temporaryPath, removeError));
                return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                    WorldServerHostRuntimeArtifactWriteError::WriteFailed);
            }
            output.close();

            std::error_code renameError;
            std::filesystem::rename(temporaryPath, outputPath, renameError);
            if (renameError)
            {
                std::error_code removeError;
                static_cast<void>(std::filesystem::remove(temporaryPath, removeError));
                return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                    WorldServerHostRuntimeArtifactWriteError::RenameFailed);
            }
            return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Success();
        }
        catch (const std::exception&)
        {
            std::error_code removeError;
            static_cast<void>(std::filesystem::remove(temporaryPath, removeError));
            return WorldResult<void, WorldServerHostRuntimeArtifactWriteError>::Failure(
                WorldServerHostRuntimeArtifactWriteError::WriteFailed);
        }
    }
} // namespace psnr::world::host
