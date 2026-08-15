#pragma once

#include "WorldGameplayReplicationPlan.h"
#include "WorldOutboundDoubleBuffer.h"
#include "WorldReplicationPlan.h"

#include <cstdint>
#include <span>

namespace psnr::world
{
    enum class WorldReplicationRecordResult : std::uint8_t
    {
        Recorded = 0,
        InvalidInput,
        CapacityExceeded,
        AllocationFailed,
        OutboundRejected,
    };

    class WorldReplicationPublisher final
    {
    public:
        [[nodiscard]] WorldReplicationRecordResult Record(
            const WorldReplicationPlan& plan, std::span<const psnr::runtime::NrSessionSendChannel> recipientChannels,
            WorldOutboundDoubleBuffer& outboundBuffer) const noexcept;

        [[nodiscard]] WorldReplicationRecordResult RecordRemoteEntityStateChunks(
            std::span<const protocol::v2::EntityStateBatch> chunks,
            const psnr::runtime::NrSessionSendChannel& recipientChannel,
            WorldOutboundDoubleBuffer& outboundBuffer) const noexcept;

        [[nodiscard]] WorldReplicationRecordResult RecordRoundResults(
            std::span<const WorldGameplayRoundResultPlan> plans,
            std::span<const psnr::runtime::NrSessionSendChannel> recipientChannels,
            WorldOutboundDoubleBuffer& outboundBuffer) const noexcept;
    };
} // namespace psnr::world
