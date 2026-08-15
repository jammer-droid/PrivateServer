#pragma once

#include "WorldExecutionModeConfig.h"

#include <cstdint>

namespace psnr::world
{
    enum class WorldWorkerKind : std::uint8_t
    {
        None = 0,
        OutboundPublisher,
        IngressPump,
        Coordinator,
    };

    enum class WorldWorkerStartupResult : std::uint8_t
    {
        Started = 0,
        InvalidArgument,
        WorkerStartFailed,
    };

    struct WorldWorkerStartupReport final
    {
        WorldWorkerStartupResult result = WorldWorkerStartupResult::Started;
        WorldWorkerKind failedWorker = WorldWorkerKind::None;
        bool outboundPublisherStarted = false;
        bool ingressPumpStarted = false;
        bool coordinatorStarted = false;
        bool rollbackCompleted = false;
    };

    // worker 시작 순서는 publisher -> pump -> coordinator이며, 실패 시 이미 시작된 worker를 역순 stop/join한다.
    class WorldWorkerStartup final
    {
    public:
        template <typename TPublisherWorker, typename TPumpWorker, typename TCoordinatorWorker>
        [[nodiscard]] static WorldWorkerStartupReport Start(const WorldExecutionModeConfig& modes,
                                                            TPublisherWorker* const publisherWorker,
                                                            TPumpWorker* const pumpWorker,
                                                            TCoordinatorWorker& coordinatorWorker)
        {
            if (!IsValid(modes) ||
                (modes.outboundMode == WorldOutboundMode::DoubleBuffered && publisherWorker == nullptr) ||
                (modes.inboundMode == WorldInboundMode::DoubleBuffered && pumpWorker == nullptr))
            {
                return WorldWorkerStartupReport{
                    WorldWorkerStartupResult::InvalidArgument, WorldWorkerKind::None, false, false, false, false,
                };
            }

            bool publisherStarted = false;
            bool pumpStarted = false;

            if (modes.outboundMode == WorldOutboundMode::DoubleBuffered)
            {
                if (!publisherWorker->Start())
                {
                    return MakeFailure(WorldWorkerKind::OutboundPublisher, false, false, false);
                }
                publisherStarted = true;
            }

            if (modes.inboundMode == WorldInboundMode::DoubleBuffered)
            {
                if (!pumpWorker->Start())
                {
                    Rollback(publisherWorker, pumpWorker, publisherStarted, false);
                    return MakeFailure(WorldWorkerKind::IngressPump, publisherStarted, false, publisherStarted);
                }
                pumpStarted = true;
            }

            if (!coordinatorWorker.Start())
            {
                Rollback(publisherWorker, pumpWorker, publisherStarted, pumpStarted);
                return MakeFailure(WorldWorkerKind::Coordinator, publisherStarted, pumpStarted,
                                   publisherStarted || pumpStarted);
            }

            return WorldWorkerStartupReport{
                WorldWorkerStartupResult::Started, WorldWorkerKind::None, publisherStarted, pumpStarted, true, false,
            };
        }

    private:
        template <typename TPublisherWorker, typename TPumpWorker>
        static void Rollback(TPublisherWorker* const publisherWorker, TPumpWorker* const pumpWorker,
                             const bool publisherStarted, const bool pumpStarted)
        {
            if (pumpStarted)
            {
                pumpWorker->RequestStop();
            }
            if (publisherStarted)
            {
                publisherWorker->RequestStop();
            }
            if (pumpStarted)
            {
                pumpWorker->Join();
            }
            if (publisherStarted)
            {
                publisherWorker->Join();
            }
        }

        [[nodiscard]] static WorldWorkerStartupReport MakeFailure(const WorldWorkerKind failedWorker,
                                                                  const bool publisherStarted, const bool pumpStarted,
                                                                  const bool rollbackCompleted) noexcept
        {
            return WorldWorkerStartupReport{
                WorldWorkerStartupResult::WorkerStartFailed,
                failedWorker,
                publisherStarted,
                pumpStarted,
                false,
                rollbackCompleted,
            };
        }
    };
} // namespace psnr::world
