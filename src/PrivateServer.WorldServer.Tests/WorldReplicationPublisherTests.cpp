#include "pch.h"

#include "WorldPacketTypes.h"
#include "WorldOutboundPublisher.h"
#include "WorldReplicationConfig.h"
#include "WorldReplicationPlan.h"
#include "WorldReplicationPublisher.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldEntityComponents MakeComponents(const WorldEntityKind kind, const float positionX)
        {
            WorldEntityComponents components;
            components.transform.positionX = positionX;
            components.motion.velocityX = kind == WorldEntityKind::StaticObstacle ? 0.0f : 1.0f;
            components.movementCapability.maxMoveSpeed = kind == WorldEntityKind::Player ? 5.0f : 0.0f;
            components.replicationMetadata.entityKind = kind;
            components.replicationMetadata.archetypeId = static_cast<std::uint32_t>(kind);
            components.replicationMetadata.primaryShapeKind = WorldShapeKind::Circle;
            components.replicationMetadata.primaryCircleRadius = 1.0f;
            return components;
        }

        [[nodiscard]] WorldEntityKey CreateEntity(WorldEntityManager* const manager,
                                                  const WorldEntityComponents& components)
        {
            WorldEntityKey key;
            EntityHandle handle;
            EXPECT_TRUE(manager->TryCreate(components, &key, &handle));
            return key;
        }

        [[nodiscard]] std::unique_ptr<WorldOutboundDoubleBuffer> CreateOutboundBuffer(
            const WorldOutboundBatchCapacity capacity)
        {
            WorldResult<std::unique_ptr<WorldOutboundDoubleBuffer>> result =
                WorldOutboundDoubleBuffer::Create(capacity);
            EXPECT_TRUE(result.Succeeded());
            return result.Failed() ? nullptr : result.TakeValue();
        }

        [[nodiscard]] protocol::v2::EntitySpawn MakeSpawn(const std::uint32_t tick, const WorldEntityKey key)
        {
            protocol::v2::EntitySpawn spawn;
            spawn.baseline.serverTick = tick;
            spawn.baseline.entityId = key.entityId;
            spawn.baseline.generation = key.generation;
            spawn.baseline.entityKind = protocol::EntityKind::Player;
            spawn.baseline.archetypeId = 1;
            spawn.baseline.primaryShapeKind = protocol::ShapeKind::Circle;
            spawn.baseline.primaryCircleRadius = 1.0f;
            spawn.baseline.maxMoveSpeed = 5.0f;
            spawn.playerId = 1;
            spawn.displayName = "Player1";
            return spawn;
        }

        [[nodiscard]] protocol::v2::EntityStateBatch MakeRemoteStateChunk(const std::uint16_t chunkIndex)
        {
            return protocol::v2::EntityStateBatch{
                55,
                7,
                chunkIndex,
                2,
                {protocol::v2::EntityStateRecord{
                    static_cast<std::uint32_t>(chunkIndex) + 1,
                    1,
                    static_cast<float>(chunkIndex),
                    2.0f,
                    0.5f,
                    1.5f,
                    3,
                    protocol::v2::BoostState::On,
                    {protocol::v2::EntityStateBodySample{static_cast<float>(chunkIndex), 2.0f}},
                }},
            };
        }

        class FakeReplicationGateway final
        {
        public:
            explicit FakeReplicationGateway(std::vector<psnr::runtime::NrGatewaySendReport> reports)
                : reports_(std::move(reports))
            {
            }

            [[nodiscard]] psnr::core::NrStatus SubmitMany(psnr::runtime::NrSessionSendChannelView,
                                                          psnr::core::NrPacketType, psnr::runtime::NrByteView,
                                                          psnr::runtime::NrGatewaySendReport& outReport) noexcept
            {
                outReport = reports_[nextReportIndex_];
                ++nextReportIndex_;
                return psnr::core::NrStatus::Success();
            }

        private:
            std::vector<psnr::runtime::NrGatewaySendReport> reports_;
            std::size_t nextReportIndex_ = 0;
        };
    } // namespace

    TEST(WorldReplicationPublisherTests, PlannerMapsAoiDiffAndSkipsStayedStaticObstacle)
    {
        WorldEntityManager manager;
        WorldEntityComponents playerComponents = MakeComponents(WorldEntityKind::Player, 10.0f);
        playerComponents.playerControl.playerId = 10;
        const WorldEntityKey enteredPlayer = CreateEntity(&manager, playerComponents);
        const WorldEntityKey stayedResource = CreateEntity(&manager, MakeComponents(WorldEntityKind::Resource, 20.0f));
        const WorldEntityKey stayedObstacle =
            CreateEntity(&manager, MakeComponents(WorldEntityKind::StaticObstacle, 30.0f));
        WorldAoiVisibilityDiff visibilityDiff;
        visibilityDiff.sessionKey = WorldSessionKey{7};
        visibilityDiff.entered = {enteredPlayer};
        visibilityDiff.stayed = {stayedObstacle, stayedResource};
        visibilityDiff.left = {WorldEntityKey{9, 2}, WorldEntityKey{4, 1}};

        const WorldReplicationPlanner planner;
        const std::vector<WorldSession> sessions{
            WorldSession{WorldSessionKey{7}, 10, enteredPlayer, "Player10"},
        };
        WorldResult<WorldReplicationPlan> result =
            planner.Build(42, std::span<const WorldAoiVisibilityDiff>{&visibilityDiff, 1}, manager, sessions, true);
        ASSERT_TRUE(result.Succeeded());
        const WorldReplicationPlan plan = result.TakeValue();
        ASSERT_EQ(plan.recipients.size(), 1u);
        const WorldReplicationRecipientPlan& recipient = plan.recipients[0];
        ASSERT_EQ(recipient.removes.size(), 2u);
        EXPECT_EQ(recipient.removes[0].entityId, 4u);
        EXPECT_EQ(recipient.removes[1].entityId, 9u);
        ASSERT_EQ(recipient.spawns.size(), 1u);
        EXPECT_EQ(recipient.spawns[0].baseline.entityId, enteredPlayer.entityId);
        EXPECT_FLOAT_EQ(recipient.spawns[0].baseline.positionX, 10.0f);
        EXPECT_EQ(recipient.spawns[0].playerId, 10u);
        EXPECT_EQ(recipient.spawns[0].displayName, "Player10");
        ASSERT_EQ(recipient.stateRecords.size(), 1u);
        EXPECT_EQ(recipient.stateRecords[0].entityId, stayedResource.entityId);
        EXPECT_FLOAT_EQ(recipient.stateRecords[0].positionX, 20.0f);
    }

    TEST(WorldReplicationPublisherTests, PlannerOmitsV1StateProjectionWhenNotRequested)
    {
        WorldEntityManager manager;
        WorldEntityComponents playerComponents = MakeComponents(WorldEntityKind::Player, 10.0f);
        playerComponents.playerControl.playerId = 10;
        const WorldEntityKey enteredPlayer = CreateEntity(&manager, playerComponents);
        WorldAoiVisibilityDiff visibilityDiff;
        visibilityDiff.sessionKey = WorldSessionKey{7};
        visibilityDiff.entered = {enteredPlayer};
        visibilityDiff.stayed = {WorldEntityKey{99, 1}};
        visibilityDiff.left = {WorldEntityKey{4, 1}};

        const WorldReplicationPlanner planner;
        const std::vector<WorldSession> sessions{
            WorldSession{WorldSessionKey{7}, 10, enteredPlayer, ""},
        };
        WorldResult<WorldReplicationPlan> result =
            planner.Build(42, std::span<const WorldAoiVisibilityDiff>{&visibilityDiff, 1}, manager, sessions, false);
        ASSERT_TRUE(result.Succeeded());
        const WorldReplicationPlan plan = result.TakeValue();
        ASSERT_EQ(plan.recipients.size(), 1u);
        const WorldReplicationRecipientPlan& recipient = plan.recipients[0];
        EXPECT_EQ(recipient.removes.size(), 1u);
        EXPECT_EQ(recipient.spawns.size(), 1u);
        EXPECT_TRUE(recipient.stateRecords.empty());
    }

    TEST(WorldReplicationPublisherTests, RecordsRemoveSpawnThenStableStateBatchChunks)
    {
        constexpr std::uint32_t ServerTick = 55;
        WorldReplicationPlan plan;
        plan.serverTick = ServerTick;
        WorldReplicationRecipientPlan recipient;
        recipient.sessionKey = WorldSessionKey{1};
        recipient.removes.push_back(
            protocol::v1::EntityRemove{ServerTick, 1, 1, protocol::EntityRemoveReason::LeftAoi});
        recipient.spawns.push_back(MakeSpawn(ServerTick, WorldEntityKey{1, 2}));
        for (std::uint32_t index = 0; index < 293; ++index)
        {
            protocol::v1::EntityStateRecord record;
            record.entityId = index + 2;
            record.generation = 1;
            record.positionX = static_cast<float>(index);
            recipient.stateRecords.push_back(record);
        }
        plan.recipients.push_back(std::move(recipient));

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{4, 4, 9000});
        ASSERT_NE(buffer, nullptr);
        const WorldReplicationPublisher publisher;
        const std::vector<psnr::runtime::NrSessionSendChannel> recipientChannels(plan.recipients.size());
        ASSERT_EQ(buffer->BeginWriteBatch(ServerTick, ServerTick, ServerTick),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(publisher.Record(plan, recipientChannels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(buffer->SealWrite(ServerTick), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(readBatch.records.size(), 4u);
        EXPECT_EQ(readBatch.records[0].packetType.value,
                  static_cast<std::uint16_t>(protocol::S2CPacketType::EntityRemove));
        EXPECT_EQ(readBatch.records[1].packetType.value,
                  static_cast<std::uint16_t>(protocol::S2CPacketType::EntitySpawn));
        EXPECT_EQ(readBatch.records[2].packetType.value,
                  static_cast<std::uint16_t>(protocol::S2CPacketType::EntityStateBatch));
        EXPECT_EQ(readBatch.records[3].packetType.value,
                  static_cast<std::uint16_t>(protocol::S2CPacketType::EntityStateBatch));

        protocol::v1::EntityStateBatch firstBatch;
        const WorldOutboundRecord& firstRecord = readBatch.records[2];
        ASSERT_EQ(protocol::v1::EntityStateBatch::Decode(
                      readBatch.payloadBytes.subspan(firstRecord.payloadOffset, firstRecord.payloadSize), &firstBatch),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(firstBatch.records.size(), protocol::v1::EntityStateBatch::Wire::MaxRecords);
        EXPECT_EQ(firstBatch.records.front().entityId, 2u);
        EXPECT_EQ(firstBatch.records.back().entityId, 293u);

        protocol::v1::EntityStateBatch secondBatch;
        const WorldOutboundRecord& secondRecord = readBatch.records[3];
        ASSERT_EQ(
            protocol::v1::EntityStateBatch::Decode(
                readBatch.payloadBytes.subspan(secondRecord.payloadOffset, secondRecord.payloadSize), &secondBatch),
            protocol::WorldProtocolError::Success);
        ASSERT_EQ(secondBatch.records.size(), 1u);
        EXPECT_EQ(secondBatch.records[0].entityId, 294u);
    }

    TEST(WorldReplicationPublisherTests, CapacityFailureDoesNotAppendPartialReplication)
    {
        WorldReplicationPlan plan;
        plan.serverTick = 1;
        WorldReplicationRecipientPlan recipient;
        recipient.sessionKey = WorldSessionKey{1};
        recipient.removes.push_back(protocol::v1::EntityRemove{1, 1, 1, protocol::EntityRemoveReason::LeftAoi});
        recipient.spawns.push_back(MakeSpawn(1, WorldEntityKey{2, 1}));
        plan.recipients.push_back(std::move(recipient));

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 100});
        ASSERT_NE(buffer, nullptr);
        const WorldReplicationPublisher publisher;
        const std::vector<psnr::runtime::NrSessionSendChannel> recipientChannels(plan.recipients.size());
        EXPECT_EQ(publisher.Record(plan, recipientChannels, *buffer), WorldReplicationRecordResult::CapacityExceeded);
        EXPECT_EQ(buffer->WritableUsage(), WorldOutboundBatchUsage{});
    }

    TEST(WorldReplicationPublisherTests, RecordsRemoteV2ChunksAfterWholeGroupCapacityCheck)
    {
        const std::vector<protocol::v2::EntityStateBatch> chunks{
            MakeRemoteStateChunk(0),
            MakeRemoteStateChunk(1),
        };
        const WorldReplicationPublisher publisher;
        const psnr::runtime::NrSessionSendChannel channel;

        std::unique_ptr<WorldOutboundDoubleBuffer> tooSmall =
            CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 128});
        ASSERT_NE(tooSmall, nullptr);
        ASSERT_EQ(tooSmall->BeginWriteBatch(55, 55, 56), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        EXPECT_EQ(publisher.RecordRemoteEntityStateChunks(chunks, channel, *tooSmall),
                  WorldReplicationRecordResult::CapacityExceeded);
        EXPECT_EQ(tooSmall->WritableUsage(), WorldOutboundBatchUsage{});

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 128});
        ASSERT_NE(buffer, nullptr);
        ASSERT_EQ(buffer->BeginWriteBatch(55, 55, 56), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(publisher.RecordRemoteEntityStateChunks(chunks, channel, *buffer),
                  WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(buffer->SealWrite(55), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch readBatch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &readBatch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(readBatch.records.size(), 2u);
        EXPECT_EQ(readBatch.replicationServerTick, 56u);
        EXPECT_EQ(readBatch.replicationRecipientCount, 1u);
        for (std::size_t index = 0; index < readBatch.records.size(); ++index)
        {
            const WorldOutboundRecord& record = readBatch.records[index];
            EXPECT_EQ(record.packetType.value, static_cast<std::uint16_t>(protocol::S2CPacketType::EntityStateBatch));
            EXPECT_EQ(record.metadata.kind, WorldOutboundRecordKind::ReplicationStateBatch);
            EXPECT_EQ(record.metadata.replicationRecipientIndex, 0u);
            protocol::v2::EntityStateBatch decoded;
            ASSERT_EQ(protocol::v2::EntityStateBatch::Decode(
                          readBatch.payloadBytes.subspan(record.payloadOffset, record.payloadSize), &decoded),
                      protocol::WorldProtocolError::Success);
            EXPECT_EQ(decoded, chunks[index]);
        }
    }

    TEST(WorldReplicationPublisherTests, RejectsRecipientChannelsThatDoNotMatchPlannedRecipients)
    {
        WorldReplicationPlan plan;
        WorldReplicationRecipientPlan recipient;
        recipient.sessionKey = WorldSessionKey{1};
        plan.recipients.push_back(std::move(recipient));
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{1, 1, 1});
        ASSERT_NE(buffer, nullptr);

        const WorldReplicationPublisher publisher;
        EXPECT_EQ(publisher.Record(plan, {}, *buffer), WorldReplicationRecordResult::InvalidInput);
        EXPECT_EQ(buffer->WritableUsage(), WorldOutboundBatchUsage{});
    }

    TEST(WorldReplicationPublisherTests, RecordsRecipientRoundResultsAsGenericOutboundPackets)
    {
        const std::vector<WorldGameplayRoundResultPlan> plans{
            WorldGameplayRoundResultPlan{WorldSessionKey{3}, protocol::v2::RoundResult{180, 4, 12, 12, {10, 20}}},
            WorldGameplayRoundResultPlan{WorldSessionKey{7}, protocol::v2::RoundResult{180, 4, 12, 5, {10, 20}}},
        };
        const std::vector<psnr::runtime::NrSessionSendChannel> channels(2);
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 128});
        ASSERT_NE(buffer, nullptr);
        const WorldReplicationPublisher publisher;

        ASSERT_EQ(publisher.RecordRoundResults(plans, channels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(buffer->SealWrite(180), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch batch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &batch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(batch.records.size(), 2u);
        EXPECT_EQ(batch.replicationRecipientCount, 0u);
        for (std::size_t index = 0; index < batch.records.size(); ++index)
        {
            const WorldOutboundRecord& record = batch.records[index];
            EXPECT_EQ(record.packetType.value, static_cast<std::uint16_t>(protocol::S2CPacketType::RoundResult));
            EXPECT_EQ(record.metadata.kind, WorldOutboundRecordKind::Generic);
            protocol::v2::RoundResult decoded;
            ASSERT_EQ(protocol::v2::RoundResult::Decode(
                          batch.payloadBytes.subspan(record.payloadOffset, record.payloadSize), &decoded),
                      protocol::WorldProtocolError::Success);
            EXPECT_EQ(decoded, plans[index].roundResult);
        }
    }

    TEST(WorldReplicationPublisherTests, RejectsInvalidLaterRoundResultBeforeRecordingEarlierResult)
    {
        std::vector<WorldGameplayRoundResultPlan> plans{
            WorldGameplayRoundResultPlan{WorldSessionKey{3}, protocol::v2::RoundResult{180, 4, 12, 12, {10, 20}}},
            WorldGameplayRoundResultPlan{WorldSessionKey{7}, protocol::v2::RoundResult{180, 4, 12, 5, {20, 10}}},
        };
        const std::vector<psnr::runtime::NrSessionSendChannel> channels(2);
        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 128});
        ASSERT_NE(buffer, nullptr);
        const WorldReplicationPublisher publisher;

        EXPECT_EQ(publisher.RecordRoundResults(plans, channels, *buffer), WorldReplicationRecordResult::InvalidInput);
        EXPECT_EQ(buffer->WritableUsage(), WorldOutboundBatchUsage{});
    }

    TEST(WorldReplicationPublisherTests, RejectsInvalidLaterPacketBeforeRecordingEarlierPacket)
    {
        WorldReplicationPlan plan;
        plan.serverTick = 1;
        WorldReplicationRecipientPlan recipient;
        recipient.sessionKey = WorldSessionKey{1};
        recipient.removes.push_back(protocol::v1::EntityRemove{1, 1, 1, protocol::EntityRemoveReason::LeftAoi});
        recipient.spawns.push_back(protocol::v2::EntitySpawn{});
        plan.recipients.push_back(std::move(recipient));

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 100});
        ASSERT_NE(buffer, nullptr);
        const std::vector<psnr::runtime::NrSessionSendChannel> recipientChannels(1);

        const WorldReplicationPublisher publisher;
        EXPECT_EQ(publisher.Record(plan, recipientChannels, *buffer), WorldReplicationRecordResult::InvalidInput);
        EXPECT_EQ(buffer->WritableUsage(), WorldOutboundBatchUsage{});
    }

    TEST(WorldReplicationPublisherTests, PublishesPlannedAndGatewayReplicationEvidence)
    {
        constexpr std::uint64_t OutboundEpoch = 9;
        constexpr std::uint32_t ServerTick = 55;
        WorldReplicationPlan plan;
        plan.serverTick = ServerTick;
        WorldReplicationRecipientPlan first;
        first.sessionKey = WorldSessionKey{1};
        first.removes.push_back(protocol::v1::EntityRemove{ServerTick, 1, 1, protocol::EntityRemoveReason::LeftAoi});
        first.spawns.push_back(MakeSpawn(ServerTick, WorldEntityKey{1, 2}));
        first.stateRecords.push_back(protocol::v1::EntityStateRecord{2, 1});
        first.stateRecords.push_back(protocol::v1::EntityStateRecord{3, 1});
        WorldReplicationRecipientPlan second;
        second.sessionKey = WorldSessionKey{2};
        second.spawns.push_back(MakeSpawn(ServerTick, WorldEntityKey{4, 1}));
        plan.recipients.push_back(std::move(first));
        plan.recipients.push_back(std::move(second));

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{4, 4, 256});
        ASSERT_NE(buffer, nullptr);
        const std::vector<psnr::runtime::NrSessionSendChannel> channels(2);
        const WorldReplicationPublisher recorder;
        ASSERT_EQ(buffer->BeginWriteBatch(OutboundEpoch, ServerTick, ServerTick),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(recorder.Record(plan, channels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(buffer->SealWrite(OutboundEpoch), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        FakeReplicationGateway gateway{
            {
                psnr::runtime::NrGatewaySendReport{1, 1, 0},
                psnr::runtime::NrGatewaySendReport{1, 0, 1},
                psnr::runtime::NrGatewaySendReport{1, 0, 1},
                psnr::runtime::NrGatewaySendReport{1, 1, 0},
            },
        };
        WorldOutboundPublisher publisher{*buffer};
        const WorldOutboundPublishReport outboundReport = publisher.PublishNext(gateway, std::chrono::milliseconds{0});
        const WorldReplicationPublishReport& report = outboundReport.replication;

        EXPECT_EQ(outboundReport.stopReason, WorldOutboundPublishStopReason::Published);
        EXPECT_EQ(outboundReport.epoch, OutboundEpoch);
        EXPECT_EQ(report.serverTick, ServerTick);
        EXPECT_EQ(report.recipientCount, 2u);
        EXPECT_EQ(report.spawnPacketCount, 2u);
        EXPECT_EQ(report.spawnEntityCount, 2u);
        EXPECT_EQ(report.stateBatchPacketCount, 1u);
        EXPECT_EQ(report.stateRecordCount, 2u);
        EXPECT_EQ(report.removePacketCount, 1u);
        EXPECT_EQ(report.removeEntityCount, 1u);
        EXPECT_EQ(report.gatewayAcceptedCount, 2u);
        EXPECT_EQ(report.gatewayRejectedCount, 2u);
        EXPECT_EQ(report.recipientsWithRejectCount, 1u);
    }

    TEST(WorldReplicationPublisherTests, ReservesDistinctMetadataAcrossMultiplePlansInOneTick)
    {
        WorldReplicationPlan firstPlan;
        firstPlan.serverTick = 55;
        WorldReplicationRecipientPlan firstRecipient;
        firstRecipient.sessionKey = WorldSessionKey{1};
        firstRecipient.spawns.push_back(MakeSpawn(55, WorldEntityKey{2, 1}));
        firstPlan.recipients.push_back(std::move(firstRecipient));
        WorldReplicationPlan secondPlan;
        secondPlan.serverTick = 55;
        WorldReplicationRecipientPlan secondRecipient;
        secondRecipient.sessionKey = WorldSessionKey{2};
        secondRecipient.spawns.push_back(MakeSpawn(55, WorldEntityKey{3, 1}));
        secondPlan.recipients.push_back(std::move(secondRecipient));

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 256});
        ASSERT_NE(buffer, nullptr);
        const std::vector<psnr::runtime::NrSessionSendChannel> channels(1);
        const WorldReplicationPublisher publisher;
        ASSERT_EQ(buffer->BeginWriteBatch(55, 55, 55), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(publisher.Record(firstPlan, channels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(publisher.Record(secondPlan, channels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(buffer->SealWrite(55), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch batch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &batch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(batch.records.size(), 2u);
        EXPECT_EQ(batch.replicationRecipientCount, 2u);
        EXPECT_EQ(batch.records[0].metadata.replicationRecipientIndex, 0u);
        EXPECT_EQ(batch.records[1].metadata.replicationRecipientIndex, 1u);
    }

    TEST(WorldReplicationPublisherTests, PreservesPayloadTicksAndUsesBatchLastTickForReplicationMetadata)
    {
        WorldReplicationPlan firstPlan;
        firstPlan.serverTick = 55;
        WorldReplicationRecipientPlan firstRecipient;
        firstRecipient.sessionKey = WorldSessionKey{1};
        firstRecipient.spawns.push_back(MakeSpawn(55, WorldEntityKey{2, 1}));
        firstPlan.recipients.push_back(std::move(firstRecipient));
        WorldReplicationPlan secondPlan;
        secondPlan.serverTick = 56;
        WorldReplicationRecipientPlan secondRecipient;
        secondRecipient.sessionKey = WorldSessionKey{2};
        secondRecipient.spawns.push_back(MakeSpawn(56, WorldEntityKey{3, 1}));
        secondPlan.recipients.push_back(std::move(secondRecipient));

        std::unique_ptr<WorldOutboundDoubleBuffer> buffer = CreateOutboundBuffer(WorldOutboundBatchCapacity{2, 2, 256});
        ASSERT_NE(buffer, nullptr);
        const std::vector<psnr::runtime::NrSessionSendChannel> channels(1);
        const WorldReplicationPublisher publisher;
        ASSERT_EQ(buffer->BeginWriteBatch(55, 55, 56), WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(publisher.Record(firstPlan, channels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(publisher.Record(secondPlan, channels, *buffer), WorldReplicationRecordResult::Recorded);
        ASSERT_EQ(buffer->SealWrite(55), WorldOutboundDoubleBufferExchangeResult::Exchanged);

        WorldOutboundReadBatch batch;
        ASSERT_EQ(buffer->WaitAcquireRead(std::chrono::milliseconds{0}, &batch),
                  WorldOutboundDoubleBufferExchangeResult::Exchanged);
        ASSERT_EQ(batch.records.size(), 2u);
        EXPECT_EQ(batch.replicationServerTick, 56u);

        protocol::v2::EntitySpawn firstDecoded;
        protocol::v2::EntitySpawn secondDecoded;
        ASSERT_EQ(protocol::v2::EntitySpawn::Decode(
                      batch.payloadBytes.subspan(batch.records[0].payloadOffset, batch.records[0].payloadSize),
                      &firstDecoded),
                  protocol::WorldProtocolError::Success);
        ASSERT_EQ(protocol::v2::EntitySpawn::Decode(
                      batch.payloadBytes.subspan(batch.records[1].payloadOffset, batch.records[1].payloadSize),
                      &secondDecoded),
                  protocol::WorldProtocolError::Success);
        EXPECT_EQ(firstDecoded.baseline.serverTick, 55u);
        EXPECT_EQ(secondDecoded.baseline.serverTick, 56u);
    }

    TEST(WorldReplicationPublisherTests, ValidatesExplicitSnapshotCadence)
    {
        const WorldReplicationConfig invalid{};
        const WorldReplicationConfig everyThreeTicks{3};

        EXPECT_FALSE(invalid.IsValid());
        EXPECT_FALSE(invalid.IsSnapshotTick(3));
        EXPECT_TRUE(everyThreeTicks.IsValid());
        EXPECT_FALSE(everyThreeTicks.IsSnapshotTick(2));
        EXPECT_TRUE(everyThreeTicks.IsSnapshotTick(3));
        EXPECT_TRUE(everyThreeTicks.IsSnapshotTick(6));
    }
} // namespace psnr::world::tests
