#include "WorldApplicationLogAdapter.h"

#include "ApplicationLogRecord.h"
#include "ApplicationLogSeverity.h"

#include <string_view>

namespace psnr::world::host
{
    std::string_view WorldApplicationLogAdapter::EventName(const WorldJoinApplicationEventKind kind) noexcept
    {
        switch (kind)
        {
        case WorldJoinApplicationEventKind::Committed:
            return "join_committed";
        case WorldJoinApplicationEventKind::RolledBack:
            return "join_rolled_back";
        case WorldJoinApplicationEventKind::RollbackFailed:
            return "join_rollback_failed";
        }

        return "join_event_unknown";
    }

    std::string_view WorldApplicationLogAdapter::ResultName(const WorldJoinApplicationEventKind kind) noexcept
    {
        switch (kind)
        {
        case WorldJoinApplicationEventKind::Committed:
            return "committed";
        case WorldJoinApplicationEventKind::RolledBack:
            return "rolled_back";
        case WorldJoinApplicationEventKind::RollbackFailed:
            return "rollback_failed";
        }

        return "unknown";
    }

    std::string_view WorldApplicationLogAdapter::FailureStageName(const WorldJoinFailureStage stage) noexcept
    {
        switch (stage)
        {
        case WorldJoinFailureStage::BaselineEncoding:
            return "baseline_encoding";
        case WorldJoinFailureStage::GameplayRegistration:
            return "gameplay_registration";
        case WorldJoinFailureStage::EntitySpawnSubmission:
            return "entity_spawn_submission";
        case WorldJoinFailureStage::AoiBaselineRecording:
            return "aoi_baseline_recording";
        case WorldJoinFailureStage::GameplayBaselineRecording:
            return "gameplay_baseline_recording";
        case WorldJoinFailureStage::WorldReadySubmission:
            return "world_ready_submission";
        case WorldJoinFailureStage::Commit:
            return "commit";
        case WorldJoinFailureStage::None:
            return {};
        }

        return "unknown_stage";
    }

    psnr::logging::ApplicationLogSeverity WorldApplicationLogAdapter::Severity(
        const WorldJoinApplicationEventKind kind) noexcept
    {
        switch (kind)
        {
        case WorldJoinApplicationEventKind::Committed:
            return psnr::logging::ApplicationLogSeverity::Info;
        case WorldJoinApplicationEventKind::RolledBack:
            return psnr::logging::ApplicationLogSeverity::Warning;
        case WorldJoinApplicationEventKind::RollbackFailed:
            return psnr::logging::ApplicationLogSeverity::Error;
        }

        return psnr::logging::ApplicationLogSeverity::Error;
    }

    std::string_view WorldApplicationLogAdapter::SessionCleanupEventName(
        const WorldSessionCleanupApplicationEventKind kind) noexcept
    {
        switch (kind)
        {
        case WorldSessionCleanupApplicationEventKind::Completed:
            return "session_cleanup_completed";
        case WorldSessionCleanupApplicationEventKind::Failed:
            return "session_cleanup_failed";
        }

        return "session_cleanup_unknown";
    }

    std::string_view WorldApplicationLogAdapter::SessionCleanupResultName(
        const WorldSessionCleanupApplicationEventKind kind) noexcept
    {
        return kind == WorldSessionCleanupApplicationEventKind::Completed ? "completed" : "failed";
    }

    std::string_view WorldApplicationLogAdapter::ProtocolCloseEventName(
        const WorldProtocolCloseApplicationEventKind kind) noexcept
    {
        switch (kind)
        {
        case WorldProtocolCloseApplicationEventKind::Requested:
            return "protocol_close_requested";
        case WorldProtocolCloseApplicationEventKind::RequestFailed:
            return "protocol_close_request_failed";
        }

        return "protocol_close_unknown";
    }

    std::string_view WorldApplicationLogAdapter::ProtocolCloseResultName(
        const WorldProtocolCloseApplicationEventKind kind) noexcept
    {
        return kind == WorldProtocolCloseApplicationEventKind::Requested ? "requested" : "failed";
    }

    std::string_view WorldApplicationLogAdapter::ProtocolCloseCauseName(const WorldProtocolCloseCause cause) noexcept
    {
        switch (cause)
        {
        case WorldProtocolCloseCause::MalformedPayload:
            return "malformed_payload";
        case WorldProtocolCloseCause::RateLimitedViolation:
            return "rate_limited_violation";
        }

        return "unknown_cause";
    }
    WorldApplicationLogAdapter::WorldApplicationLogAdapter(const psnr::logging::ApplicationLogHandle logHandle) noexcept
        : logHandle_(logHandle)
    {
    }

    void WorldApplicationLogAdapter::RecordJoin(const WorldJoinApplicationEvent& event) noexcept
    {
        try
        {
            psnr::logging::ApplicationLogRecord record{};
            record.severity = Severity(event.kind);
            record.component = "world";
            record.event = EventName(event.kind);
            record.context.serverTick = event.serverTick;
            record.context.worldSessionKey = event.sessionKey.value;
            record.context.entityId = event.entityKey.entityId;
            record.context.entityGeneration = event.entityKey.generation;
            record.context.operation = "join";
            record.context.result = ResultName(event.kind);

            const std::string_view failureStage = FailureStageName(event.failureStage);
            if (!failureStage.empty())
            {
                record.context.error = failureStage;
            }

            logHandle_.Log(record);
        }
        catch (...)
        {
            // Observability must not change World state transitions when record construction cannot allocate.
        }
    }

    void WorldApplicationLogAdapter::RecordSessionCleanup(const WorldSessionCleanupApplicationEvent& event) noexcept
    {
        try
        {
            psnr::logging::ApplicationLogRecord record{};
            record.severity = event.kind == WorldSessionCleanupApplicationEventKind::Completed
                                  ? psnr::logging::ApplicationLogSeverity::Info
                                  : psnr::logging::ApplicationLogSeverity::Error;
            record.component = "world";
            record.event = SessionCleanupEventName(event.kind);
            record.context.serverTick = event.serverTick;
            record.context.worldSessionKey = event.sessionKey.value;
            if (event.entityKey.IsValid())
            {
                record.context.entityId = event.entityKey.entityId;
                record.context.entityGeneration = event.entityKey.generation;
            }
            record.context.operation = "session_cleanup";
            record.context.result = SessionCleanupResultName(event.kind);
            if (event.kind == WorldSessionCleanupApplicationEventKind::Failed)
            {
                record.context.error = "world_state_cleanup";
            }

            logHandle_.Log(record);
        }
        catch (...)
        {
            // Observability must not change World state transitions when record construction cannot allocate.
        }
    }

    void WorldApplicationLogAdapter::RecordProtocolClose(const WorldProtocolCloseApplicationEvent& event) noexcept
    {
        try
        {
            psnr::logging::ApplicationLogRecord record{};
            record.severity = event.kind == WorldProtocolCloseApplicationEventKind::Requested
                                  ? psnr::logging::ApplicationLogSeverity::Warning
                                  : psnr::logging::ApplicationLogSeverity::Error;
            record.component = "world";
            record.event = ProtocolCloseEventName(event.kind);
            record.context.serverTick = event.serverTick;
            record.context.worldSessionKey = event.sessionKey.value;
            record.context.operation = "protocol_close";
            record.context.result = ProtocolCloseResultName(event.kind);
            record.context.error = ProtocolCloseCauseName(event.cause);

            logHandle_.Log(record);
        }
        catch (...)
        {
            // Observability must not change World state transitions when record construction cannot allocate.
        }
    }
} // namespace psnr::world::host
