#pragma once

#include "ApplicationLogHandle.h"
#include "ApplicationLogSeverity.h"
#include "WorldDoubleBufferedTickCoordinator.h"
#include "WorldDoubleBufferedWorkers.h"
#include "WorldExecutionStorage.h"
#include "WorldIngressPump.h"
#include "WorldOutboundPublisher.h"
#include "WorldWorkerShutdown.h"
#include "WorldWorkerStartup.h"

#include <PrivateServer/NetworkRuntime/NrStatus.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace psnr::logging
{
    class ApplicationLogger;
}

namespace psnr::world::host
{
    class WorldServerHostLog final
    {
    public:
        WorldServerHostLog(psnr::logging::ApplicationLogger& logger,
                           psnr::logging::ApplicationLogHandle worldLog) noexcept;

        void HostEvent(psnr::logging::ApplicationLogSeverity severity, std::string_view event,
                       std::string_view operation, std::string_view result) const;
        void WorldEvent(psnr::logging::ApplicationLogSeverity severity, std::string_view event,
                        std::string_view operation, std::string_view result, std::string_view error = {}) const;
        void HostNativeFailure(std::string_view event, std::string_view operation, std::string_view error,
                               std::uint32_t nativeErrorCode) const;
        void RuntimeFailure(std::string_view operation, psnr::core::NrStatus status) const;
        void NetworkListening(std::uint32_t channelId, std::string_view channelName,
                              const std::array<std::uint8_t, 4>& bindAddress, std::uint16_t port) const;
        void StorageCreateFailure(WorldErrorCode errorCode) const;
        void WorldReady() const;
        void WorkerStartupFailure(WorldWorkerKind worker) const;
        void WorkerStoppedUnexpectedly(WorldWorkerKind worker, WorldConcreteWorkerStopReason reason) const;
        void RecordUnexpectedWorkerStopDetail(WorldConcreteWorkerStopReason publisherReason,
                                              WorldConcreteWorkerStopReason ingressReason,
                                              WorldConcreteWorkerStopReason coordinatorReason,
                                              const WorldDoubleBufferedTickReport& tickReport,
                                              const WorldOutboundBatchUsage& outboundUsage,
                                              const WorldOutboundBatchCapacity& outboundCapacity,
                                              const WorldOutboundAppendFailure& appendFailure) const;
        void RecordWorkerShutdown(const WorldWorkerShutdownReport& shutdownReport,
                                  const WorldIngressPumpMetrics& ingressMetrics,
                                  const WorldDoubleBufferedTickMetrics& tickMetrics,
                                  const WorldOutboundPublisherMetrics& outboundMetrics) const;
        [[nodiscard]] int Complete(int result) const;

    private:
        [[nodiscard]] static std::string_view WorkerName(WorldWorkerKind worker) noexcept;
        [[nodiscard]] static std::string_view WorldErrorCodeName(WorldErrorCode errorCode) noexcept;
        [[nodiscard]] static std::string_view WorkerStopReasonName(WorldConcreteWorkerStopReason reason) noexcept;
        [[nodiscard]] static std::string_view TickStopReasonName(WorldDoubleBufferedTickStopReason reason) noexcept;
        [[nodiscard]] static std::string_view TickProcessResultName(WorldTickProcessResult result) noexcept;
        [[nodiscard]] static std::string_view OutboundExchangeResultName(
            WorldOutboundDoubleBufferExchangeResult result) noexcept;
        [[nodiscard]] static std::string_view OutboundWriteOperationName(
            WorldOutboundWriteOperation operation) noexcept;
        [[nodiscard]] static std::string_view OutboundAppendResultName(WorldOutboundAppendResult result) noexcept;
        [[nodiscard]] static std::string_view WorkerShutdownResultName(WorldWorkerShutdownResult result) noexcept;

        psnr::logging::ApplicationLogger& logger_;
        psnr::logging::ApplicationLogHandle worldLog_;
    };
} // namespace psnr::world::host
