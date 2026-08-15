#include "pch.h"

#include "WorldReplicationPublisher.h"

#include "WorldPacketTypes.h"

#include <algorithm>
#include <array>
#include <limits>

namespace psnr::world
{
    namespace
    {
        struct RemoveEncodeContext final
        {
            const protocol::v1::EntityRemove* value = nullptr;
        };

        struct SpawnEncodeContext final
        {
            const protocol::v2::EntitySpawn* value = nullptr;
        };

        struct StateEncodeContext final
        {
            std::uint32_t serverTick = 0;
            std::span<const protocol::v1::EntityStateRecord> records{};
        };

        struct StateV2EncodeContext final
        {
            const protocol::v2::EntityStateBatch* value = nullptr;
        };

        struct RoundResultEncodeContext final
        {
            const protocol::v2::RoundResult* value = nullptr;
        };

        [[nodiscard]] psnr::core::NrPacketType PacketType(const protocol::S2CPacketType value) noexcept
        {
            return psnr::core::NrPacketType{static_cast<std::uint16_t>(value)};
        }

        [[nodiscard]] bool EncodeRemovePayload(void* const context, const std::span<std::byte> output) noexcept
        {
            const RemoveEncodeContext* encodeContext = static_cast<const RemoveEncodeContext*>(context);
            return protocol::v1::EntityRemove::Encode(*encodeContext->value, output) ==
                   protocol::WorldProtocolError::Success;
        }

        [[nodiscard]] bool EncodeSpawnPayload(void* const context, const std::span<std::byte> output) noexcept
        {
            const SpawnEncodeContext* encodeContext = static_cast<const SpawnEncodeContext*>(context);
            return protocol::v2::EntitySpawn::Encode(*encodeContext->value, output) ==
                   protocol::WorldProtocolError::Success;
        }

        [[nodiscard]] bool EncodeStatePayload(void* const context, const std::span<std::byte> output) noexcept
        {
            const StateEncodeContext* encodeContext = static_cast<const StateEncodeContext*>(context);
            return protocol::v1::EntityStateBatch::Encode(encodeContext->serverTick, encodeContext->records, output) ==
                   protocol::WorldProtocolError::Success;
        }

        [[nodiscard]] bool EncodeStateV2Payload(void* const context, const std::span<std::byte> output) noexcept
        {
            const StateV2EncodeContext* encodeContext = static_cast<const StateV2EncodeContext*>(context);
            return protocol::v2::EntityStateBatch::Encode(*encodeContext->value, output) ==
                   protocol::WorldProtocolError::Success;
        }

        [[nodiscard]] bool EncodeRoundResultPayload(void* const context, const std::span<std::byte> output) noexcept
        {
            const RoundResultEncodeContext* encodeContext = static_cast<const RoundResultEncodeContext*>(context);
            return protocol::v2::RoundResult::Encode(*encodeContext->value, output) ==
                   protocol::WorldProtocolError::Success;
        }

        [[nodiscard]] bool CalculateAndValidate(const WorldReplicationPlan& plan,
                                                WorldOutboundBatchUsage* const outRequired)
        {
            WorldOutboundBatchUsage required{};

            for (const WorldReplicationRecipientPlan& recipient : plan.recipients)
            {
                for (const protocol::v1::EntityRemove& remove : recipient.removes)
                {
                    if (protocol::v1::EntityRemove::Validate(remove) != protocol::WorldProtocolError::Success)
                    {
                        return false;
                    }
                    ++required.recordCount;
                    ++required.recipientCount;
                    required.payloadByteCount += protocol::v1::EntityRemove::Wire::PayloadBytes;
                }
                for (const protocol::v2::EntitySpawn& spawn : recipient.spawns)
                {
                    const std::size_t payloadBytes =
                        protocol::v2::EntitySpawn::CalculatePayloadBytes(spawn.displayName);
                    if (payloadBytes == 0 ||
                        protocol::v2::EntitySpawn::Validate(spawn) != protocol::WorldProtocolError::Success)
                    {
                        return false;
                    }
                    ++required.recordCount;
                    ++required.recipientCount;
                    required.payloadByteCount += payloadBytes;
                }

                std::size_t offset = 0;
                while (offset < recipient.stateRecords.size())
                {
                    const std::size_t count = std::min(recipient.stateRecords.size() - offset,
                                                       protocol::v1::EntityStateBatch::Wire::MaxRecords);
                    const std::span<const protocol::v1::EntityStateRecord> records{
                        recipient.stateRecords.data() + offset,
                        count,
                    };
                    if (protocol::v1::EntityStateBatch::Validate(records) != protocol::WorldProtocolError::Success)
                    {
                        return false;
                    }
                    ++required.recordCount;
                    ++required.recipientCount;
                    required.payloadByteCount += protocol::v1::EntityStateBatch::Wire::CalculatePayloadBytes(count);
                    offset += count;
                }
            }

            *outRequired = required;
            return true;
        }
    } // namespace

    WorldReplicationRecordResult WorldReplicationPublisher::Record(
        const WorldReplicationPlan& plan, const std::span<const psnr::runtime::NrSessionSendChannel> recipientChannels,
        WorldOutboundDoubleBuffer& outboundBuffer) const noexcept
    {
        if (recipientChannels.size() != plan.recipients.size())
        {
            return WorldReplicationRecordResult::InvalidInput;
        }

        try
        {
            WorldOutboundBatchUsage required{};
            if (!CalculateAndValidate(plan, &required))
            {
                return WorldReplicationRecordResult::InvalidInput;
            }

            const WorldOutboundBatchUsage usage = outboundBuffer.WritableUsage();
            const WorldOutboundBatchCapacity capacity = outboundBuffer.CapacityPerSlot();
            if (required.recordCount > capacity.recordCount - usage.recordCount ||
                required.recipientCount > capacity.recipientCount - usage.recipientCount ||
                required.payloadByteCount > capacity.payloadByteCount - usage.payloadByteCount)
            {
                return WorldReplicationRecordResult::CapacityExceeded;
            }
            if (required.recordCount == 0)
            {
                return WorldReplicationRecordResult::Recorded;
            }
            std::uint32_t firstRecipientIndex = 0;
            if (plan.recipients.size() > std::numeric_limits<std::uint32_t>::max() ||
                outboundBuffer.ReserveReplicationRecipients(static_cast<std::uint32_t>(plan.recipients.size()),
                                                            &firstRecipientIndex) !=
                    WorldOutboundAppendResult::Appended)
            {
                return WorldReplicationRecordResult::OutboundRejected;
            }

            for (std::size_t recipientIndex = 0; recipientIndex < plan.recipients.size(); ++recipientIndex)
            {
                const WorldReplicationRecipientPlan& recipient = plan.recipients[recipientIndex];
                const std::array<psnr::runtime::NrSessionSendChannel, 1> channel{
                    recipientChannels[recipientIndex],
                };
                for (const protocol::v1::EntityRemove& remove : recipient.removes)
                {
                    RemoveEncodeContext context{&remove};
                    if (outboundBuffer.TryAppendEncoded(
                            PacketType(protocol::S2CPacketType::EntityRemove), channel,
                            protocol::v1::EntityRemove::Wire::PayloadBytes, EncodeRemovePayload, &context,
                            WorldOutboundRecordMetadata{
                                WorldOutboundRecordKind::ReplicationRemove,
                                firstRecipientIndex + static_cast<std::uint32_t>(recipientIndex),
                                1,
                            }) != WorldOutboundAppendResult::Appended)
                    {
                        return WorldReplicationRecordResult::OutboundRejected;
                    }
                }
                for (const protocol::v2::EntitySpawn& spawn : recipient.spawns)
                {
                    SpawnEncodeContext context{&spawn};
                    const std::size_t payloadBytes =
                        protocol::v2::EntitySpawn::CalculatePayloadBytes(spawn.displayName);
                    if (outboundBuffer.TryAppendEncoded(
                            PacketType(protocol::S2CPacketType::EntitySpawn), channel,
                            payloadBytes, EncodeSpawnPayload, &context,
                            WorldOutboundRecordMetadata{
                                WorldOutboundRecordKind::ReplicationSpawn,
                                firstRecipientIndex + static_cast<std::uint32_t>(recipientIndex),
                                1,
                            }) != WorldOutboundAppendResult::Appended)
                    {
                        return WorldReplicationRecordResult::OutboundRejected;
                    }
                }

                std::size_t offset = 0;
                while (offset < recipient.stateRecords.size())
                {
                    const std::size_t count = std::min(recipient.stateRecords.size() - offset,
                                                       protocol::v1::EntityStateBatch::Wire::MaxRecords);
                    StateEncodeContext context{
                        plan.serverTick,
                        std::span<const protocol::v1::EntityStateRecord>{
                            recipient.stateRecords.data() + offset,
                            count,
                        },
                    };
                    if (outboundBuffer.TryAppendEncoded(
                            PacketType(protocol::S2CPacketType::EntityStateBatch), channel,
                            protocol::v1::EntityStateBatch::Wire::CalculatePayloadBytes(count), EncodeStatePayload,
                            &context,
                            WorldOutboundRecordMetadata{
                                WorldOutboundRecordKind::ReplicationStateBatch,
                                firstRecipientIndex + static_cast<std::uint32_t>(recipientIndex),
                                static_cast<std::uint32_t>(count),
                            }) != WorldOutboundAppendResult::Appended)
                    {
                        return WorldReplicationRecordResult::OutboundRejected;
                    }
                    offset += count;
                }
            }

            return WorldReplicationRecordResult::Recorded;
        }
        catch (...)
        {
            return WorldReplicationRecordResult::AllocationFailed;
        }
    }

    WorldReplicationRecordResult WorldReplicationPublisher::RecordRemoteEntityStateChunks(
        const std::span<const protocol::v2::EntityStateBatch> chunks,
        const psnr::runtime::NrSessionSendChannel& recipientChannel,
        WorldOutboundDoubleBuffer& outboundBuffer) const noexcept
    {
        if (chunks.empty())
        {
            return WorldReplicationRecordResult::Recorded;
        }

        std::size_t requiredPayloadBytes = 0;
        for (const protocol::v2::EntityStateBatch& chunk : chunks)
        {
            const std::size_t payloadBytes = protocol::v2::EntityStateBatch::Wire::CalculatePayloadBytes(chunk.records);
            if (payloadBytes == 0 ||
                protocol::v2::EntityStateBatch::Validate(chunk) != protocol::WorldProtocolError::Success)
            {
                return WorldReplicationRecordResult::InvalidInput;
            }
            requiredPayloadBytes += payloadBytes;
        }

        const WorldOutboundBatchUsage usage = outboundBuffer.WritableUsage();
        const WorldOutboundBatchCapacity capacity = outboundBuffer.CapacityPerSlot();
        if (chunks.size() > capacity.recordCount - usage.recordCount ||
            chunks.size() > capacity.recipientCount - usage.recipientCount ||
            requiredPayloadBytes > capacity.payloadByteCount - usage.payloadByteCount)
        {
            return WorldReplicationRecordResult::CapacityExceeded;
        }

        std::uint32_t recipientIndex = 0;
        if (outboundBuffer.ReserveReplicationRecipients(1, &recipientIndex) != WorldOutboundAppendResult::Appended)
        {
            return WorldReplicationRecordResult::OutboundRejected;
        }
        const std::span<const psnr::runtime::NrSessionSendChannel> channel{&recipientChannel, 1};
        for (const protocol::v2::EntityStateBatch& chunk : chunks)
        {
            const std::size_t payloadBytes = protocol::v2::EntityStateBatch::Wire::CalculatePayloadBytes(chunk.records);
            StateV2EncodeContext context{&chunk};
            if (outboundBuffer.TryAppendEncoded(PacketType(protocol::S2CPacketType::EntityStateBatch), channel,
                                                payloadBytes, EncodeStateV2Payload, &context,
                                                WorldOutboundRecordMetadata{
                                                    WorldOutboundRecordKind::ReplicationStateBatch,
                                                    recipientIndex,
                                                    static_cast<std::uint32_t>(chunk.records.size()),
                                                }) != WorldOutboundAppendResult::Appended)
            {
                return WorldReplicationRecordResult::OutboundRejected;
            }
        }
        return WorldReplicationRecordResult::Recorded;
    }

    WorldReplicationRecordResult WorldReplicationPublisher::RecordRoundResults(
        const std::span<const WorldGameplayRoundResultPlan> plans,
        const std::span<const psnr::runtime::NrSessionSendChannel> recipientChannels,
        WorldOutboundDoubleBuffer& outboundBuffer) const noexcept
    {
        if (plans.size() != recipientChannels.size())
        {
            return WorldReplicationRecordResult::InvalidInput;
        }

        std::size_t requiredPayloadBytes = 0;
        for (const WorldGameplayRoundResultPlan& plan : plans)
        {
            const std::size_t payloadBytes =
                protocol::v2::RoundResult::Wire::CalculatePayloadBytes(plan.roundResult.winnerPlayerIds.size());
            if (!plan.sessionKey.IsValid() || payloadBytes == 0 ||
                protocol::v2::RoundResult::Validate(plan.roundResult) != protocol::WorldProtocolError::Success)
            {
                return WorldReplicationRecordResult::InvalidInput;
            }
            requiredPayloadBytes += payloadBytes;
        }

        const WorldOutboundBatchUsage usage = outboundBuffer.WritableUsage();
        const WorldOutboundBatchCapacity capacity = outboundBuffer.CapacityPerSlot();
        if (plans.size() > capacity.recordCount - usage.recordCount ||
            plans.size() > capacity.recipientCount - usage.recipientCount ||
            requiredPayloadBytes > capacity.payloadByteCount - usage.payloadByteCount)
        {
            return WorldReplicationRecordResult::CapacityExceeded;
        }

        for (std::size_t index = 0; index < plans.size(); ++index)
        {
            const WorldGameplayRoundResultPlan& plan = plans[index];
            const std::span<const psnr::runtime::NrSessionSendChannel> channel{&recipientChannels[index], 1};
            const std::size_t payloadBytes =
                protocol::v2::RoundResult::Wire::CalculatePayloadBytes(plan.roundResult.winnerPlayerIds.size());
            RoundResultEncodeContext context{&plan.roundResult};
            if (outboundBuffer.TryAppendEncoded(PacketType(protocol::S2CPacketType::RoundResult), channel, payloadBytes,
                                                EncodeRoundResultPayload,
                                                &context) != WorldOutboundAppendResult::Appended)
            {
                return WorldReplicationRecordResult::OutboundRejected;
            }
        }
        return WorldReplicationRecordResult::Recorded;
    }
} // namespace psnr::world
