#pragma once

#include "WorldOutboundDoubleBuffer.h"
#include "WorldReplicationPublishReport.h"

#include <PrivateServer/NetworkRuntime/NrErrorCode.h>
#include <PrivateServer/NetworkRuntime/NrStatus.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace psnr::world
{
    enum class WorldOutboundPublishStopReason : std::uint8_t
    {
        Published = 0,
        NoBatch,
        AcquireFailed,
        PublicationFailed,
        ReleaseFailed,
    };

    struct WorldOutboundPublishReport final
    {
        WorldOutboundPublishStopReason stopReason = WorldOutboundPublishStopReason::Published;
        std::uint64_t epoch = 0;
        std::size_t recordCount = 0;
        std::size_t processedRecordCount = 0;
        std::size_t discardedRecordCount = 0;
        std::uint64_t attemptedRecipientCount = 0;
        std::uint64_t acceptedRecipientCount = 0;
        std::uint64_t rejectedRecipientCount = 0;
        std::uint64_t discardedRecipientCount = 0;
        WorldOutboundDoubleBufferExchangeResult exchangeResult = WorldOutboundDoubleBufferExchangeResult::Exchanged;
        psnr::core::NrStatus publicationStatus{};
        WorldReplicationPublishReport replication{};
    };

    struct WorldOutboundPublisherMetrics final
    {
        std::uint64_t publishedBatchCount = 0;
        std::uint64_t publishedRecordCount = 0;
        std::uint64_t discardedRecordCount = 0;
        std::uint64_t acceptedRecipientCount = 0;
        std::uint64_t rejectedRecipientCount = 0;
        std::uint64_t discardedRecipientCount = 0;
        std::uint64_t publicationFailureCount = 0;
        std::uint64_t recordHighWatermark = 0;
        std::uint64_t recipientHighWatermark = 0;
        std::uint64_t payloadByteHighWatermark = 0;
    };

    // publisher worker가 호출하는 한 번의 publication 동작이다.
    // batch storage를 직접 참조해 record 순서대로 SubmitMany하고 임시 recipient/payload vector를 만들지 않는다.
    class WorldOutboundPublisher final
    {
    public:
        explicit WorldOutboundPublisher(WorldOutboundDoubleBuffer& buffer) noexcept
            : buffer_(buffer)
        {
        }

        template <typename TGateway>
        [[nodiscard]] WorldOutboundPublishReport PublishNext(TGateway& gateway,
                                                             const std::chrono::milliseconds waitTimeout)
        {
            WorldOutboundReadBatch batch;
            const WorldOutboundDoubleBufferExchangeResult acquireResult = buffer_.WaitAcquireRead(waitTimeout, &batch);
            if (acquireResult == WorldOutboundDoubleBufferExchangeResult::TimedOut ||
                acquireResult == WorldOutboundDoubleBufferExchangeResult::Empty)
            {
                return MakeReport(WorldOutboundPublishStopReason::NoBatch, 0, 0, 0, 0, 0, 0, 0, 0, acquireResult,
                                  psnr::core::NrStatus::Success());
            }
            if (acquireResult != WorldOutboundDoubleBufferExchangeResult::Exchanged)
            {
                return MakeReport(WorldOutboundPublishStopReason::AcquireFailed, 0, 0, 0, 0, 0, 0, 0, 0, acquireResult,
                                  psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState));
            }

            std::size_t processedRecordCount = 0;
            std::uint64_t processedRecipientCount = 0;
            std::uint64_t attemptedRecipientCount = 0;
            std::uint64_t acceptedRecipientCount = 0;
            std::uint64_t rejectedRecipientCount = 0;
            psnr::core::NrStatus publicationStatus = psnr::core::NrStatus::Success();
            WorldReplicationPublishReport replicationReport = BuildReplicationReport(batch);
            std::vector<std::uint8_t> rejectedReplicationRecipients(batch.replicationRecipientCount, 0);

            for (const WorldOutboundRecord& record : batch.records)
            {
                if (!IsRecordRangeValid(record, batch))
                {
                    publicationStatus = psnr::core::NrStatus::Failure(psnr::core::NrErrorCode::InvalidState);
                    break;
                }

                psnr::runtime::NrGatewaySendReport sendReport;
                publicationStatus = gateway.SubmitMany(
                    psnr::runtime::NrSessionSendChannelView{
                        batch.recipients.data() + record.recipientOffset,
                        record.recipientCount,
                    },
                    record.packetType,
                    psnr::runtime::NrByteView{
                        batch.payloadBytes.data() + record.payloadOffset,
                        record.payloadSize,
                    },
                    sendReport);
                if (publicationStatus.Failed())
                {
                    break;
                }

                ++processedRecordCount;
                processedRecipientCount += record.recipientCount;
                attemptedRecipientCount += sendReport.attempted;
                acceptedRecipientCount += sendReport.accepted;
                rejectedRecipientCount += sendReport.rejected;
                if (record.metadata.kind != WorldOutboundRecordKind::Generic)
                {
                    replicationReport.gatewayAcceptedCount += sendReport.accepted;
                    replicationReport.gatewayRejectedCount += sendReport.rejected;
                    const std::size_t recipientIndex = record.metadata.replicationRecipientIndex;
                    if (sendReport.rejected > 0 && rejectedReplicationRecipients[recipientIndex] == 0)
                    {
                        rejectedReplicationRecipients[recipientIndex] = 1;
                        ++replicationReport.recipientsWithRejectCount;
                    }
                }
            }

            const WorldOutboundDoubleBufferExchangeResult releaseResult = buffer_.ReleaseRead(batch.epoch);
            if (releaseResult != WorldOutboundDoubleBufferExchangeResult::Exchanged)
            {
                return MakeReport(WorldOutboundPublishStopReason::ReleaseFailed, batch.epoch, batch.records.size(),
                                  processedRecordCount, 0, attemptedRecipientCount, acceptedRecipientCount,
                                  rejectedRecipientCount, 0, releaseResult, publicationStatus, replicationReport);
            }

            const std::size_t discardedRecordCount = batch.records.size() - processedRecordCount;
            const std::uint64_t discardedRecipientCount =
                static_cast<std::uint64_t>(batch.recipients.size()) - processedRecipientCount;
            metrics_.publishedRecordCount += processedRecordCount;
            metrics_.acceptedRecipientCount += acceptedRecipientCount;
            metrics_.rejectedRecipientCount += rejectedRecipientCount;
            metrics_.discardedRecordCount += discardedRecordCount;
            metrics_.discardedRecipientCount += discardedRecipientCount;
            UpdateHighWatermarks(batch);

            if (publicationStatus.Failed())
            {
                ++metrics_.publicationFailureCount;
                return MakeReport(WorldOutboundPublishStopReason::PublicationFailed, batch.epoch, batch.records.size(),
                                  processedRecordCount, discardedRecordCount, attemptedRecipientCount,
                                  acceptedRecipientCount, rejectedRecipientCount, discardedRecipientCount,
                                  releaseResult, publicationStatus, replicationReport);
            }

            ++metrics_.publishedBatchCount;
            return MakeReport(WorldOutboundPublishStopReason::Published, batch.epoch, batch.records.size(),
                              processedRecordCount, discardedRecordCount, attemptedRecipientCount,
                              acceptedRecipientCount, rejectedRecipientCount, discardedRecipientCount, releaseResult,
                              publicationStatus, replicationReport);
        }

        [[nodiscard]] WorldOutboundPublisherMetrics Metrics() const noexcept
        {
            return metrics_;
        }

        void Close() noexcept
        {
            buffer_.Close();
        }

        void FinishWrites() noexcept
        {
            buffer_.FinishWrites();
        }

    private:
        [[nodiscard]] static bool IsRecordRangeValid(const WorldOutboundRecord& record,
                                                     const WorldOutboundReadBatch& batch) noexcept
        {
            const std::size_t recipientOffset = record.recipientOffset;
            const std::size_t payloadOffset = record.payloadOffset;
            const bool validReplicationMetadata =
                record.metadata.kind == WorldOutboundRecordKind::Generic ||
                (record.metadata.entityCount > 0 &&
                 record.metadata.replicationRecipientIndex < batch.replicationRecipientCount);
            return record.packetType.value != 0 && record.recipientCount > 0 && validReplicationMetadata &&
                   recipientOffset <= batch.recipients.size() &&
                   record.recipientCount <= batch.recipients.size() - recipientOffset &&
                   payloadOffset <= batch.payloadBytes.size() &&
                   record.payloadSize <= batch.payloadBytes.size() - payloadOffset;
        }

        [[nodiscard]] static WorldReplicationPublishReport BuildReplicationReport(
            const WorldOutboundReadBatch& batch) noexcept
        {
            WorldReplicationPublishReport report;
            report.serverTick = batch.replicationServerTick;
            report.recipientCount = batch.replicationRecipientCount;
            for (const WorldOutboundRecord& record : batch.records)
            {
                switch (record.metadata.kind)
                {
                case WorldOutboundRecordKind::ReplicationSpawn:
                    ++report.spawnPacketCount;
                    report.spawnEntityCount += record.metadata.entityCount;
                    break;
                case WorldOutboundRecordKind::ReplicationStateBatch:
                    ++report.stateBatchPacketCount;
                    report.stateRecordCount += record.metadata.entityCount;
                    break;
                case WorldOutboundRecordKind::ReplicationRemove:
                    ++report.removePacketCount;
                    report.removeEntityCount += record.metadata.entityCount;
                    break;
                case WorldOutboundRecordKind::Generic:
                default:
                    break;
                }
            }
            return report;
        }

        void UpdateHighWatermarks(const WorldOutboundReadBatch& batch) noexcept
        {
            if (batch.records.size() > metrics_.recordHighWatermark)
            {
                metrics_.recordHighWatermark = batch.records.size();
            }
            if (batch.recipients.size() > metrics_.recipientHighWatermark)
            {
                metrics_.recipientHighWatermark = batch.recipients.size();
            }
            if (batch.payloadBytes.size() > metrics_.payloadByteHighWatermark)
            {
                metrics_.payloadByteHighWatermark = batch.payloadBytes.size();
            }
        }

        [[nodiscard]] static WorldOutboundPublishReport MakeReport(
            const WorldOutboundPublishStopReason stopReason, const std::uint64_t epoch, const std::size_t recordCount,
            const std::size_t processedRecordCount, const std::size_t discardedRecordCount,
            const std::uint64_t attemptedRecipientCount, const std::uint64_t acceptedRecipientCount,
            const std::uint64_t rejectedRecipientCount, const std::uint64_t discardedRecipientCount,
            const WorldOutboundDoubleBufferExchangeResult exchangeResult, const psnr::core::NrStatus publicationStatus,
            const WorldReplicationPublishReport replication = {}) noexcept
        {
            return WorldOutboundPublishReport{
                stopReason,
                epoch,
                recordCount,
                processedRecordCount,
                discardedRecordCount,
                attemptedRecipientCount,
                acceptedRecipientCount,
                rejectedRecipientCount,
                discardedRecipientCount,
                exchangeResult,
                publicationStatus,
                replication,
            };
        }

        WorldOutboundDoubleBuffer& buffer_;
        WorldOutboundPublisherMetrics metrics_;
    };
} // namespace psnr::world
