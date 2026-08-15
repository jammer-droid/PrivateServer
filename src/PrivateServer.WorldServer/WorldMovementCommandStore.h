#pragma once

#include "WorldExecutionModeConfig.h"
#include "WorldMovementCommand.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace psnr::world
{
    enum class WorldMovementCommandStoreResult : std::uint8_t
    {
        Stored = 0,
        Replaced,
        InvalidCommand,
        DuplicateTargetTick,
    };

    struct WorldMovementCommandStoreMetrics final
    {
        std::uint64_t storedCommandCount = 0;
        std::uint64_t replacedCommandCount = 0;
        std::uint64_t takenCommandCount = 0;
        std::uint64_t canceledCommandCount = 0;
        std::uint32_t maximumCommandAgeTicks = 0;
    };

    // Admission을 통과한 movement command를 target server tick별로 보관한다.
    // 같은 session의 같은 target tick에는 최초 command만 유지한다.
    class WorldMovementCommandStore final
    {
    public:
        [[nodiscard]] WorldMovementCommandStoreResult TryStore(WorldInboundMode inboundMode,
                                                               const WorldMovementCommand& command);
        [[nodiscard]] WorldMovementCommandStoreResult TryStore(const WorldMovementCommand& command);
        [[nodiscard]] bool TryTake(std::uint32_t targetServerTick, std::vector<WorldMovementCommand>* outCommands);
        [[nodiscard]] std::size_t RemoveSession(WorldSessionKey sessionKey) noexcept;

        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] WorldMovementCommandStoreMetrics Metrics() const noexcept;

    private:
        using CommandBucket = std::unordered_map<WorldSessionKey, WorldMovementCommand, WorldSessionKeyHash>;
        using TargetTickToCommandBucketMap = std::map<std::uint32_t, CommandBucket>; // key: serverTick

        TargetTickToCommandBucketMap targetTickToCommands_;
        std::size_t commandCount_ = 0;
        WorldMovementCommandStoreMetrics metrics_;
    };
} // namespace psnr::world
