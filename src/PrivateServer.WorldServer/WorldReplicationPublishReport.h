#pragma once

#include <cstddef>
#include <cstdint>

namespace psnr::world
{
    struct WorldReplicationPublishReport final
    {
        std::uint64_t serverTick = 0;
        std::size_t recipientCount = 0;
        std::size_t spawnPacketCount = 0;
        std::size_t spawnEntityCount = 0;
        std::size_t stateBatchPacketCount = 0;
        std::size_t stateRecordCount = 0;
        std::size_t removePacketCount = 0;
        std::size_t removeEntityCount = 0;
        std::uint64_t gatewayAcceptedCount = 0;
        std::uint64_t gatewayRejectedCount = 0;
        std::size_t recipientsWithRejectCount = 0;
    };
} // namespace psnr::world
