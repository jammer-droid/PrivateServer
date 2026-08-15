#pragma once

#include "ApplicationLogHandle.h"
#include "ApplicationLogSeverity.h"
#include "WorldApplicationEventSink.h"

#include <string_view>

namespace psnr::world::host
{
    class WorldApplicationLogAdapter final : public IWorldApplicationEventSink
    {
    public:
        explicit WorldApplicationLogAdapter(psnr::logging::ApplicationLogHandle logHandle) noexcept;

        void RecordJoin(const WorldJoinApplicationEvent& event) noexcept override;
        void RecordSessionCleanup(const WorldSessionCleanupApplicationEvent& event) noexcept override;
        void RecordProtocolClose(const WorldProtocolCloseApplicationEvent& event) noexcept override;

    private:
        [[nodiscard]] static std::string_view EventName(WorldJoinApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view ResultName(WorldJoinApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view FailureStageName(WorldJoinFailureStage stage) noexcept;
        [[nodiscard]] static psnr::logging::ApplicationLogSeverity Severity(
            WorldJoinApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view SessionCleanupEventName(
            WorldSessionCleanupApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view SessionCleanupResultName(
            WorldSessionCleanupApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view ProtocolCloseEventName(
            WorldProtocolCloseApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view ProtocolCloseResultName(
            WorldProtocolCloseApplicationEventKind kind) noexcept;
        [[nodiscard]] static std::string_view ProtocolCloseCauseName(WorldProtocolCloseCause cause) noexcept;

        psnr::logging::ApplicationLogHandle logHandle_;
    };
} // namespace psnr::world::host
