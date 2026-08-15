#include "WorldServerHostLog.h"

#include "ApplicationLogHealth.h"
#include "ApplicationLogRecord.h"
#include "ApplicationLogger.h"

#include <cstdint>
#include <string>

namespace psnr::world::host
{
    WorldServerHostLog::WorldServerHostLog(psnr::logging::ApplicationLogger& logger,
                                           psnr::logging::ApplicationLogHandle worldLog) noexcept
        : logger_(logger)
        , worldLog_(worldLog)
    {
    }

    void WorldServerHostLog::HostEvent(const psnr::logging::ApplicationLogSeverity severity,
                                       const std::string_view event, const std::string_view operation,
                                       const std::string_view result) const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = severity;
        record.component = "host";
        record.event = event;
        record.context.operation = operation;
        record.context.result = result;
        logger_.Log(record);
    }

    void WorldServerHostLog::WorldEvent(const psnr::logging::ApplicationLogSeverity severity,
                                        const std::string_view event, const std::string_view operation,
                                        const std::string_view result, const std::string_view error) const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = severity;
        record.component = "world";
        record.event = event;
        record.context.operation = operation;
        record.context.result = result;
        if (!error.empty())
        {
            record.context.error = error;
        }
        worldLog_.Log(record);
    }

    void WorldServerHostLog::HostNativeFailure(const std::string_view event, const std::string_view operation,
                                               const std::string_view error, const std::uint32_t nativeErrorCode) const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = psnr::logging::ApplicationLogSeverity::Error;
        record.component = "host";
        record.event = event;
        record.context.operation = operation;
        record.context.result = "failed";
        record.context.error = error;
        record.context.nativeErrorCode = nativeErrorCode;
        logger_.Log(record);
    }

    void WorldServerHostLog::RuntimeFailure(const std::string_view operation, const psnr::core::NrStatus status) const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = psnr::logging::ApplicationLogSeverity::Error;
        record.component = "host";
        record.event = "runtime_operation_failed";
        record.context.operation = operation;
        record.context.result = "failed";
        record.context.error = "runtime_failure";
        record.context.nativeErrorCode = status.NativeErrorCode();
        record.message = "errorCode=" + std::to_string(static_cast<std::uint32_t>(status.ErrorCode()));
        logger_.Log(record);
    }

    void WorldServerHostLog::NetworkListening(const std::uint32_t channelId, const std::string_view channelName,
                                              const std::array<std::uint8_t, 4>& bindAddress,
                                              const std::uint16_t port) const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = psnr::logging::ApplicationLogSeverity::Info;
        record.component = "host";
        record.event = "network_listening";
        record.context.operation = "nr_server_start";
        record.context.result = "completed";
        record.message = "channelId=" + std::to_string(channelId) + " channelName=" + std::string{channelName} +
                         " endpoint=" + std::to_string(bindAddress[0]) + "." + std::to_string(bindAddress[1]) + "." +
                         std::to_string(bindAddress[2]) + "." + std::to_string(bindAddress[3]) + ":" +
                         std::to_string(port);
        logger_.Log(record);
    }

    void WorldServerHostLog::StorageCreateFailure(const WorldErrorCode errorCode) const
    {
        WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "storage_create_failed", "world_storage_create",
                   "failed", WorldErrorCodeName(errorCode));
    }

    void WorldServerHostLog::WorldReady() const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = psnr::logging::ApplicationLogSeverity::Info;
        record.component = "world";
        record.event = "ready";
        record.context.operation = "world_worker_startup";
        record.context.result = "completed";
        record.message = "inbound=double_buffered outbound=triple_buffered";
        worldLog_.Log(record);

        HostEvent(psnr::logging::ApplicationLogSeverity::Info, "listen_ready", "host_startup", "completed");
    }

    void WorldServerHostLog::WorkerStartupFailure(const WorldWorkerKind worker) const
    {
        WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "worker_startup_failed", WorkerName(worker), "failed",
                   "start_failed");
    }

    void WorldServerHostLog::WorkerStoppedUnexpectedly(const WorldWorkerKind worker,
                                                       const WorldConcreteWorkerStopReason reason) const
    {
        WorldEvent(psnr::logging::ApplicationLogSeverity::Error, "worker_stopped_unexpectedly", WorkerName(worker),
                   "failed", WorkerStopReasonName(reason));
    }

    void WorldServerHostLog::RecordUnexpectedWorkerStopDetail(const WorldConcreteWorkerStopReason publisherReason,
                                                              const WorldConcreteWorkerStopReason ingressReason,
                                                              const WorldConcreteWorkerStopReason coordinatorReason,
                                                              const WorldDoubleBufferedTickReport& tickReport,
                                                              const WorldOutboundBatchUsage& outboundUsage,
                                                              const WorldOutboundBatchCapacity& outboundCapacity,
                                                              const WorldOutboundAppendFailure& appendFailure) const
    {
        psnr::logging::ApplicationLogRecord record{};
        record.severity = psnr::logging::ApplicationLogSeverity::Error;
        record.component = "world";
        record.event = "worker_stop_detail";
        record.context.operation = "world_worker_observation";
        record.context.result = "failed";
        record.context.error = TickStopReasonName(tickReport.stopReason);
        record.message =
            "publisherReason=" + std::string{WorkerStopReasonName(publisherReason)} +
            " ingressReason=" + std::string{WorkerStopReasonName(ingressReason)} +
            " coordinatorReason=" + std::string{WorkerStopReasonName(coordinatorReason)} +
            " tickStopReason=" + std::string{TickStopReasonName(tickReport.stopReason)} +
            " tickProcessResult=" + std::string{TickProcessResultName(tickReport.tickProcessResult)} +
            " outboundExchangeResult=" + std::string{OutboundExchangeResultName(tickReport.outboundExchangeResult)} +
            " epoch=" + std::to_string(tickReport.plan.epoch) +
            " serverTick=" + std::to_string(tickReport.plan.serverTick) +
            " processedTickCount=" + std::to_string(tickReport.processedTickCount) +
            " outboundRecords=" + std::to_string(outboundUsage.recordCount) + "/" +
            std::to_string(outboundCapacity.recordCount) +
            " outboundRecipients=" + std::to_string(outboundUsage.recipientCount) + "/" +
            std::to_string(outboundCapacity.recipientCount) +
            " outboundPayloadBytes=" + std::to_string(outboundUsage.payloadByteCount) + "/" +
            std::to_string(outboundCapacity.payloadByteCount) +
            " appendFailurePresent=" + (appendFailure.Present() ? std::string{"true"} : std::string{"false"}) +
            " appendOperation=" + std::string{OutboundWriteOperationName(appendFailure.operation)} +
            " appendResult=" + std::string{OutboundAppendResultName(appendFailure.result)} +
            " appendPacketType=" + std::to_string(appendFailure.packetType.value) +
            " appendServerTick=" + std::to_string(appendFailure.serverTick) +
            " existingReplicationServerTick=" + std::to_string(appendFailure.existingReplicationServerTick) +
            " existingReplicationRecipients=" + std::to_string(appendFailure.existingReplicationRecipientCount) +
            " replicationRecipientIndex=" + std::to_string(appendFailure.replicationRecipientIndex) +
            " entityCount=" + std::to_string(appendFailure.entityCount) +
            " requestedRecipients=" + std::to_string(appendFailure.requestedRecipientCount) +
            " requestedPayloadBytes=" + std::to_string(appendFailure.requestedPayloadByteCount) +
            " appendFailureRecords=" + std::to_string(appendFailure.usage.recordCount) +
            " appendFailureRecipients=" + std::to_string(appendFailure.usage.recipientCount) +
            " appendFailurePayloadBytes=" + std::to_string(appendFailure.usage.payloadByteCount);
        worldLog_.Log(record);
    }

    void WorldServerHostLog::RecordWorkerShutdown(const WorldWorkerShutdownReport& shutdownReport,
                                                  const WorldIngressPumpMetrics& ingressMetrics,
                                                  const WorldDoubleBufferedTickMetrics& tickMetrics,
                                                  const WorldOutboundPublisherMetrics& outboundMetrics) const
    {
        const bool completed = shutdownReport.result == WorldWorkerShutdownResult::Completed;
        WorldEvent(completed ? psnr::logging::ApplicationLogSeverity::Info
                             : psnr::logging::ApplicationLogSeverity::Error,
                   completed ? "shutdown_completed" : "shutdown_failed", "world_worker_shutdown",
                   completed ? "completed" : "failed",
                   completed ? std::string_view{} : WorkerShutdownResultName(shutdownReport.result));

        if (outboundMetrics.publicationFailureCount > 0)
        {
            psnr::logging::ApplicationLogRecord publicationFailure{};
            publicationFailure.severity = psnr::logging::ApplicationLogSeverity::Warning;
            publicationFailure.component = "world";
            publicationFailure.event = "outbound_publication_failed";
            publicationFailure.context.operation = "outbound_publication";
            publicationFailure.context.result = "failed";
            publicationFailure.context.error = "submission_failure";
            publicationFailure.message = "failureCount=" + std::to_string(outboundMetrics.publicationFailureCount);
            worldLog_.Log(publicationFailure);
        }

        psnr::logging::ApplicationLogRecord metricsRecord{};
        metricsRecord.severity =
            completed ? psnr::logging::ApplicationLogSeverity::Info : psnr::logging::ApplicationLogSeverity::Warning;
        metricsRecord.component = "world";
        metricsRecord.event = "terminal_metrics";
        metricsRecord.context.operation = "world_shutdown";
        metricsRecord.context.result = completed ? "completed" : "failed";
        metricsRecord.message =
            "ticks=" + std::to_string(tickMetrics.processedTickCount) +
            " ingressEvents=" + std::to_string(ingressMetrics.drainedEventCount) +
            " catchUpBatches=" + std::to_string(tickMetrics.catchUpBatchCount) +
            " catchUpTicks=" + std::to_string(tickMetrics.catchUpTickCount) +
            " backlogTicks=" + std::to_string(tickMetrics.currentBacklogTickCount) +
            " maxBacklogTicks=" + std::to_string(tickMetrics.maximumBacklogTickCount) +
            " overrunBatches=" + std::to_string(tickMetrics.overrunBatchCount) +
            " consecutiveOverrunBatches=" + std::to_string(tickMetrics.consecutiveOverrunBatchCount) +
            " maxTickStartLagNs=" + std::to_string(tickMetrics.maximumTickStartLagNanoseconds) +
            " maxTickDurationNs=" + std::to_string(tickMetrics.maximumTickDurationNanoseconds) +
            " snapshots=" + std::to_string(tickMetrics.publishedSnapshotCount) +
            " suppressedSnapshots=" + std::to_string(tickMetrics.suppressedCatchUpSnapshotCount) +
            " outboundBatches=" + std::to_string(outboundMetrics.publishedBatchCount) +
            " outboundRecords=" + std::to_string(outboundMetrics.publishedRecordCount) +
            " outboundDiscardedRecords=" + std::to_string(outboundMetrics.discardedRecordCount) +
            " outboundAcceptedRecipients=" + std::to_string(outboundMetrics.acceptedRecipientCount) +
            " outboundRejectedRecipients=" + std::to_string(outboundMetrics.rejectedRecipientCount) +
            " outboundDiscardedRecipients=" + std::to_string(outboundMetrics.discardedRecipientCount) +
            " outboundPublicationFailures=" + std::to_string(outboundMetrics.publicationFailureCount) +
            " terminalAccepted=" + std::to_string(shutdownReport.terminalIngress.acceptedEventCount) +
            " terminalClosed=" + std::to_string(shutdownReport.terminalIngress.closedEventCount) +
            " terminalDiscardedPackets=" + std::to_string(shutdownReport.terminalIngress.discardedPacketCount) +
            " terminalUnsupported=" + std::to_string(shutdownReport.terminalIngress.unsupportedEventCount);
        worldLog_.Log(metricsRecord);
    }

    int WorldServerHostLog::Complete(const int result) const
    {
        HostEvent(result == 0 ? psnr::logging::ApplicationLogSeverity::Info
                              : psnr::logging::ApplicationLogSeverity::Error,
                  result == 0 ? "host_stopped" : "host_failed", "host_shutdown", result == 0 ? "completed" : "failed");

        const psnr::logging::ApplicationLogHealth health = logger_.CaptureHealth();
        const bool degraded = !health.started || health.fileSinkFailed || health.consoleSinkFailed ||
                              health.droppedQueueFull > 0 || health.discardedAfterSinkFailure > 0;
        psnr::logging::ApplicationLogRecord healthRecord{};
        healthRecord.severity =
            degraded ? psnr::logging::ApplicationLogSeverity::Warning : psnr::logging::ApplicationLogSeverity::Info;
        healthRecord.component = "host";
        healthRecord.event = "logging_health_summary";
        healthRecord.context.operation = "application_logging";
        healthRecord.context.result = degraded ? "degraded" : "healthy";
        healthRecord.message =
            std::string{"started="} + (health.started ? "true" : "false") +
            " fileSinkFailed=" + (health.fileSinkFailed ? "true" : "false") +
            " consoleSinkFailed=" + (health.consoleSinkFailed ? "true" : "false") +
            " attempted=" + std::to_string(health.attempted) + " filtered=" + std::to_string(health.filtered) +
            " enqueued=" + std::to_string(health.enqueued) + " consumed=" + std::to_string(health.consumed) +
            " droppedQueueFull=" + std::to_string(health.droppedQueueFull) +
            " discardedAfterSinkFailure=" + std::to_string(health.discardedAfterSinkFailure) +
            " currentQueueDepth=" + std::to_string(health.currentQueueDepth) +
            " maximumQueueDepth=" + std::to_string(health.maximumQueueDepth);
        logger_.Log(healthRecord);
        return result;
    }

    std::string_view WorldServerHostLog::WorkerName(const WorldWorkerKind worker) noexcept
    {
        switch (worker)
        {
        case WorldWorkerKind::OutboundPublisher:
            return "outbound_publisher";
        case WorldWorkerKind::IngressPump:
            return "ingress_pump";
        case WorldWorkerKind::Coordinator:
            return "coordinator";
        case WorldWorkerKind::None:
        default:
            return "unknown_worker";
        }
    }

    std::string_view WorldServerHostLog::WorldErrorCodeName(const WorldErrorCode errorCode) noexcept
    {
        switch (errorCode)
        {
        case WorldErrorCode::InvalidArgument:
            return "invalid_argument";
        case WorldErrorCode::InvalidState:
            return "invalid_state";
        case WorldErrorCode::InvalidConfig:
            return "invalid_config";
        case WorldErrorCode::AllocationFailed:
            return "allocation_failed";
        case WorldErrorCode::CapacityExceeded:
            return "capacity_exceeded";
        case WorldErrorCode::DependencyFailure:
            return "dependency_failure";
        case WorldErrorCode::OperationFailed:
            return "operation_failed";
        default:
            return "unknown_world_error";
        }
    }

    std::string_view WorldServerHostLog::WorkerStopReasonName(const WorldConcreteWorkerStopReason reason) noexcept
    {
        switch (reason)
        {
        case WorldConcreteWorkerStopReason::NotStarted:
            return "not_started";
        case WorldConcreteWorkerStopReason::Running:
            return "running";
        case WorldConcreteWorkerStopReason::StopRequested:
            return "stop_requested";
        case WorldConcreteWorkerStopReason::Completed:
            return "completed";
        case WorldConcreteWorkerStopReason::OperationFailed:
            return "operation_failed";
        case WorldConcreteWorkerStopReason::StartFailed:
            return "start_failed";
        default:
            return "unknown_stop_reason";
        }
    }

    std::string_view WorldServerHostLog::TickStopReasonName(const WorldDoubleBufferedTickStopReason reason) noexcept
    {
        switch (reason)
        {
        case WorldDoubleBufferedTickStopReason::Completed:
            return "completed";
        case WorldDoubleBufferedTickStopReason::InvalidConfig:
            return "invalid_config";
        case WorldDoubleBufferedTickStopReason::IngressAcquireFailed:
            return "ingress_acquire_failed";
        case WorldDoubleBufferedTickStopReason::TickProcessFailed:
            return "tick_process_failed";
        case WorldDoubleBufferedTickStopReason::OutboundWriteFailed:
            return "outbound_write_failed";
        case WorldDoubleBufferedTickStopReason::OutboundPrepareFailed:
            return "outbound_prepare_failed";
        case WorldDoubleBufferedTickStopReason::OutboundSealFailed:
            return "outbound_seal_failed";
        case WorldDoubleBufferedTickStopReason::IngressReleaseFailed:
            return "ingress_release_failed";
        case WorldDoubleBufferedTickStopReason::SequenceExhausted:
            return "sequence_exhausted";
        default:
            return "unknown_tick_stop_reason";
        }
    }

    std::string_view WorldServerHostLog::TickProcessResultName(const WorldTickProcessResult result) noexcept
    {
        switch (result)
        {
        case WorldTickProcessResult::Processed:
            return "processed";
        case WorldTickProcessResult::InvalidFixedDelta:
            return "invalid_fixed_delta";
        case WorldTickProcessResult::InvalidControlMovementConfig:
            return "invalid_control_movement_config";
        case WorldTickProcessResult::InvalidSessionSet:
            return "invalid_session_set";
        case WorldTickProcessResult::NonSequentialServerTick:
            return "non_sequential_server_tick";
        case WorldTickProcessResult::EntityStateInvariantViolation:
            return "entity_state_invariant_violation";
        case WorldTickProcessResult::PhysicsInitialPenetration:
            return "physics_initial_penetration";
        case WorldTickProcessResult::PhysicsComputeFailed:
            return "physics_compute_failed";
        case WorldTickProcessResult::BodyTrailSampleFailed:
            return "body_trail_sample_failed";
        case WorldTickProcessResult::BodyFinalizeFailed:
            return "body_finalize_failed";
        case WorldTickProcessResult::CollisionProjectionFailed:
            return "collision_projection_failed";
        case WorldTickProcessResult::CollisionQueryFailed:
            return "collision_query_failed";
        case WorldTickProcessResult::CollisionDeathResolveFailed:
            return "collision_death_resolve_failed";
        case WorldTickProcessResult::InvalidActiveArea:
            return "invalid_active_area";
        case WorldTickProcessResult::ActiveAreaDeathCollectFailed:
            return "active_area_death_collect_failed";
        case WorldTickProcessResult::PlayerSpawnPlanFailed:
            return "player_spawn_plan_failed";
        case WorldTickProcessResult::PlayerSpawnReservationFailed:
            return "player_spawn_reservation_failed";
        case WorldTickProcessResult::GameplayProcessFailed:
            return "gameplay_process_failed";
        default:
            return "unknown_tick_process_result";
        }
    }

    std::string_view WorldServerHostLog::OutboundExchangeResultName(
        const WorldOutboundDoubleBufferExchangeResult result) noexcept
    {
        switch (result)
        {
        case WorldOutboundDoubleBufferExchangeResult::Exchanged:
            return "exchanged";
        case WorldOutboundDoubleBufferExchangeResult::Empty:
            return "empty";
        case WorldOutboundDoubleBufferExchangeResult::InvalidArgument:
            return "invalid_argument";
        case WorldOutboundDoubleBufferExchangeResult::InvalidState:
            return "invalid_state";
        case WorldOutboundDoubleBufferExchangeResult::Busy:
            return "busy";
        case WorldOutboundDoubleBufferExchangeResult::TimedOut:
            return "timed_out";
        default:
            return "unknown_outbound_exchange_result";
        }
    }

    std::string_view WorldServerHostLog::OutboundWriteOperationName(
        const WorldOutboundWriteOperation operation) noexcept
    {
        switch (operation)
        {
        case WorldOutboundWriteOperation::None:
            return "none";
        case WorldOutboundWriteOperation::Append:
            return "append";
        case WorldOutboundWriteOperation::AppendEncoded:
            return "append_encoded";
        case WorldOutboundWriteOperation::ReserveReplicationRecipients:
            return "reserve_replication_recipients";
        default:
            return "unknown_write_operation";
        }
    }

    std::string_view WorldServerHostLog::OutboundAppendResultName(const WorldOutboundAppendResult result) noexcept
    {
        switch (result)
        {
        case WorldOutboundAppendResult::Appended:
            return "appended";
        case WorldOutboundAppendResult::InvalidArgument:
            return "invalid_argument";
        case WorldOutboundAppendResult::InvalidState:
            return "invalid_state";
        case WorldOutboundAppendResult::CapacityExceeded:
            return "capacity_exceeded";
        case WorldOutboundAppendResult::EncodingFailed:
            return "encoding_failed";
        default:
            return "unknown_append_result";
        }
    }

    std::string_view WorldServerHostLog::WorkerShutdownResultName(const WorldWorkerShutdownResult result) noexcept
    {
        switch (result)
        {
        case WorldWorkerShutdownResult::Completed:
            return "completed";
        case WorldWorkerShutdownResult::InvalidArgument:
            return "invalid_argument";
        case WorldWorkerShutdownResult::GameplayStopTimedOut:
            return "gameplay_stop_timed_out";
        case WorldWorkerShutdownResult::OutboundDrainFailed:
            return "outbound_drain_failed";
        case WorldWorkerShutdownResult::TerminalIngressDrainFailed:
            return "terminal_ingress_drain_failed";
        case WorldWorkerShutdownResult::RuntimeShutdownFailed:
            return "runtime_shutdown_failed";
        default:
            return "unknown_shutdown_result";
        }
    }
} // namespace psnr::world::host
