#pragma once

#include "WorldSessionRegistry.h"

#include <cstdint>

namespace psnr::world
{
    enum class WorldJoinApplicationEventKind : std::uint8_t
    {
        Committed = 0,
        RolledBack,
        RollbackFailed,
    };

    enum class WorldJoinFailureStage : std::uint8_t
    {
        None = 0,
        BaselineEncoding,
        GameplayRegistration,
        EntitySpawnSubmission,
        AoiBaselineRecording,
        GameplayBaselineRecording,
        WorldReadySubmission,
        Commit,
    };

    struct WorldJoinApplicationEvent final
    {
        WorldJoinApplicationEventKind kind = WorldJoinApplicationEventKind::Committed;
        WorldJoinFailureStage failureStage = WorldJoinFailureStage::None;
        std::uint32_t serverTick = 0;
        WorldSessionKey sessionKey{};
        WorldEntityKey entityKey{};
    };

    enum class WorldSessionCleanupApplicationEventKind : std::uint8_t
    {
        Completed = 0,
        Failed,
    };

    struct WorldSessionCleanupApplicationEvent final
    {
        WorldSessionCleanupApplicationEventKind kind = WorldSessionCleanupApplicationEventKind::Completed;
        std::uint32_t serverTick = 0;
        WorldSessionKey sessionKey{};
        WorldEntityKey entityKey{};
    };

    enum class WorldProtocolCloseApplicationEventKind : std::uint8_t
    {
        Requested = 0,
        RequestFailed,
    };

    enum class WorldProtocolCloseCause : std::uint8_t
    {
        MalformedPayload = 0,
        RateLimitedViolation,
    };

    struct WorldProtocolCloseApplicationEvent final
    {
        WorldProtocolCloseApplicationEventKind kind = WorldProtocolCloseApplicationEventKind::Requested;
        WorldProtocolCloseCause cause = WorldProtocolCloseCause::MalformedPayload;
        std::uint32_t serverTick = 0;
        WorldSessionKey sessionKey{};
    };

    class IWorldApplicationEventSink
    {
    public:
        virtual ~IWorldApplicationEventSink() = default;

        virtual void RecordJoin(const WorldJoinApplicationEvent& event) noexcept = 0;
        virtual void RecordSessionCleanup(const WorldSessionCleanupApplicationEvent& event) noexcept = 0;
        virtual void RecordProtocolClose(const WorldProtocolCloseApplicationEvent& event) noexcept = 0;
    };
} // namespace psnr::world
