#pragma once

#include "NrDiagnosticsConfig.h"
#include "NrEndpoint.h"
#include "NrPacketType.h"

#include <cstddef>
#include <cstdint>

namespace psnr::runtime
{
    using psnr::core::NrPacketType;

    inline constexpr int NrDefaultListenBacklog = 64;
    inline constexpr std::size_t NrDefaultActorMailboxCapacity = 64;
    inline constexpr std::size_t NrDefaultPendingSendQueueCapacity = 1024;
    inline constexpr std::size_t NrDefaultMaxSessionCount = 1024;
    inline constexpr std::size_t NrDefaultToWorldEventCapacity = 128;
    inline constexpr std::size_t NrDefaultPayloadPoolBlockCount = 1024;

    struct NrServerPayloadPoolConfig final
    {
        std::size_t payload64BlockCount = NrDefaultPayloadPoolBlockCount;
        std::size_t payload256BlockCount = NrDefaultPayloadPoolBlockCount;
        std::size_t payload1024BlockCount = NrDefaultPayloadPoolBlockCount;
        std::size_t payload8192BlockCount = NrDefaultPayloadPoolBlockCount;
        std::size_t payloadRefControlBlockCount = NrDefaultPayloadPoolBlockCount;
    };

    struct NrPacketTypeView final
    {
        const NrPacketType* data = nullptr;
        std::uint32_t size = 0;
    };

    struct NrServerConfig final
    {
        NrEndpoint bindEndpoint{};
        int listenBacklog = NrDefaultListenBacklog;
        std::uint32_t acceptSlotCount = 1;
        std::size_t actorMailboxCapacity = NrDefaultActorMailboxCapacity;
        std::size_t pendingSendQueueCapacity = NrDefaultPendingSendQueueCapacity;
        std::size_t maxSessionCount = NrDefaultMaxSessionCount;
        std::size_t toWorldEventCapacity = NrDefaultToWorldEventCapacity;
        NrServerPayloadPoolConfig payloadPools{};
        // Appended to Runtime defaults and borrowed during NrServer::Create. The Runtime copies every packet type.
        NrPacketTypeView additionalWorldIngressPacketTypes{};
        NrDiagnosticsConfig diagnostics{};
    };

} // namespace psnr::runtime
