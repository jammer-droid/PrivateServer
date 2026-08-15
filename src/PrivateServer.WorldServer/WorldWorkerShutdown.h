#pragma once

#include "WorldExecutionModeConfig.h"
#include "WorldIngressTerminalConsumer.h"

#include <cstdint>

namespace psnr::world
{
    enum class WorldWorkerShutdownResult : std::uint8_t
    {
        Completed = 0,
        InvalidArgument,
        GameplayStopTimedOut,
        OutboundDrainFailed,
        TerminalIngressDrainFailed,
        RuntimeShutdownFailed,
    };

    struct WorldWorkerShutdownReport final
    {
        WorldWorkerShutdownResult result = WorldWorkerShutdownResult::Completed;
        bool gameplayStopped = false;
        bool outboundDrained = false;
        bool terminalIngressEnabled = false;
        bool terminalIngressDrained = false;
        bool runtimeShutdownRequested = false;
        bool runtimeShutdownSucceeded = false;
        bool workersJoined = false;
        WorldIngressTerminalConsumeReport terminalIngress{};
    };

    // 새 gameplay/output 생성을 먼저 막고 outbound를 비운 뒤에만 ingress terminal consume과 Runtime 종료를 시작한다.
    class WorldWorkerShutdown final
    {
    public:
        template <typename TRuntime, typename TPublisherWorker, typename TPumpWorker, typename TCoordinatorWorker>
        [[nodiscard]] static WorldWorkerShutdownReport Run(const WorldExecutionModeConfig& modes, TRuntime& runtime,
                                                           TPublisherWorker* const publisherWorker,
                                                           TPumpWorker* const pumpWorker,
                                                           TCoordinatorWorker& coordinatorWorker)
        {
            if (!IsValid(modes) ||
                (modes.outboundMode == WorldOutboundMode::DoubleBuffered && publisherWorker == nullptr) ||
                (modes.inboundMode == WorldInboundMode::DoubleBuffered && pumpWorker == nullptr))
            {
                WorldWorkerShutdownReport invalidReport;
                invalidReport.result = WorldWorkerShutdownResult::InvalidArgument;
                return invalidReport;
            }

            WorldWorkerShutdownReport report;
            report.gameplayStopped = coordinatorWorker.StopGameplayAndWait();

            report.outboundDrained = true;
            if (modes.outboundMode == WorldOutboundMode::DoubleBuffered)
            {
                report.outboundDrained = publisherWorker->DrainAndStop();
            }

            coordinatorWorker.EnterTerminalConsume();
            if (modes.inboundMode == WorldInboundMode::DoubleBuffered)
            {
                pumpWorker->EnterTerminalDrain();
            }
            report.terminalIngressEnabled = true;

            report.runtimeShutdownRequested = true;
            report.runtimeShutdownSucceeded = runtime.RequestStopAndShutdown();

            if (modes.inboundMode == WorldInboundMode::DoubleBuffered)
            {
                pumpWorker->Join();
            }
            coordinatorWorker.Join();
            report.workersJoined = true;

            report.terminalIngressDrained =
                modes.inboundMode != WorldInboundMode::DoubleBuffered ||
                (pumpWorker->TerminalDrainSucceeded() && coordinatorWorker.TerminalConsumeSucceeded());
            if (modes.inboundMode == WorldInboundMode::DoubleBuffered)
            {
                report.terminalIngress = coordinatorWorker.TerminalConsumeReport();
            }

            report.result = !report.gameplayStopped           ? WorldWorkerShutdownResult::GameplayStopTimedOut
                            : !report.outboundDrained         ? WorldWorkerShutdownResult::OutboundDrainFailed
                            : !report.terminalIngressDrained  ? WorldWorkerShutdownResult::TerminalIngressDrainFailed
                            : report.runtimeShutdownSucceeded ? WorldWorkerShutdownResult::Completed
                                                              : WorldWorkerShutdownResult::RuntimeShutdownFailed;
            return report;
        }
    };
} // namespace psnr::world
