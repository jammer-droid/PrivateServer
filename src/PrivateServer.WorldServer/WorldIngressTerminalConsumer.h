#pragma once

#include "WorldExecutionModeConfig.h"
#include "WorldIngressDoubleBuffer.h"

#include <PrivateServer/NetworkRuntime/NrToWorldEvent.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace psnr::world
{
    struct WorldIngressTerminalConsumeReport final
    {
        std::size_t acceptedEventCount = 0;
        std::size_t closedEventCount = 0;
        std::size_t discardedPacketCount = 0;
        std::size_t unsupportedEventCount = 0;
    };

    // gameplay가 정지한 뒤에는 lifecycle 정리만 유지하고 PacketReceived는 decode하지 않고 폐기한다.
    class WorldIngressTerminalConsumer final
    {
    public:
        template <typename TEvent, typename TEventConsumer>
        [[nodiscard]] static WorldIngressTerminalConsumeReport Consume(const std::span<const TEvent> events,
                                                                       TEventConsumer& eventConsumer)
        {
            WorldIngressTerminalConsumeReport report;
            for (const TEvent& event : events)
            {
                switch (event.Kind())
                {
                case psnr::runtime::NrToWorldEventKind::SessionAccepted:
                    static_cast<void>(eventConsumer.Handle(event, WorldInboundMode::DoubleBuffered));
                    ++report.acceptedEventCount;
                    break;
                case psnr::runtime::NrToWorldEventKind::SessionClosed:
                    static_cast<void>(eventConsumer.Handle(event, WorldInboundMode::DoubleBuffered));
                    ++report.closedEventCount;
                    break;
                case psnr::runtime::NrToWorldEventKind::PacketReceived:
                    ++report.discardedPacketCount;
                    break;
                case psnr::runtime::NrToWorldEventKind::None:
                    ++report.unsupportedEventCount;
                    break;
                }
            }
            return report;
        }

        template <typename TEventConsumer>
        [[nodiscard]] static WorldIngressTerminalConsumeReport Consume(const WorldIngressReadBatch& batch,
                                                                       TEventConsumer& eventConsumer)
        {
            return Consume(batch.events, eventConsumer);
        }
    };
} // namespace psnr::world
