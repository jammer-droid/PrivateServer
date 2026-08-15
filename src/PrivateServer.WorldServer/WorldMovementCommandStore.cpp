#include "pch.h"

#include "WorldMovementCommandStore.h"

#include <utility>

namespace psnr::world
{
    WorldMovementCommandStoreResult WorldMovementCommandStore::TryStore(const WorldInboundMode inboundMode,
                                                                        const WorldMovementCommand& command)
    {
        if ((inboundMode != WorldInboundMode::TargetServerTick && inboundMode != WorldInboundMode::DoubleBuffered) ||
            !command.sessionKey.IsValid() || command.playerId == 0 || !command.entityKey.IsValid() ||
            command.admittedServerTick > command.targetServerTick)
        {
            return WorldMovementCommandStoreResult::InvalidCommand;
        }

        CommandBucket& commands = targetTickToCommands_[command.targetServerTick];
        const CommandBucket::iterator existing = commands.find(command.sessionKey);
        if (existing != commands.end())
        {
            if (inboundMode == WorldInboundMode::DoubleBuffered)
            {
                existing->second = command; // 같은 session, 같은 epoch 에 입력이 여러번 들어온 경우 최신으로 덮음
                ++metrics_.replacedCommandCount;
                return WorldMovementCommandStoreResult::Replaced;
            }
            return WorldMovementCommandStoreResult::DuplicateTargetTick;
        }

        commands.emplace(command.sessionKey, command);
        ++commandCount_;
        ++metrics_.storedCommandCount;
        return WorldMovementCommandStoreResult::Stored;
    }

    WorldMovementCommandStoreResult WorldMovementCommandStore::TryStore(const WorldMovementCommand& command)
    {
        return TryStore(WorldInboundMode::TargetServerTick, command);
    }

    // targetServerTick 에 있는 Movementcommand 목록 반환
    bool WorldMovementCommandStore::TryTake(const std::uint32_t targetServerTick,
                                            std::vector<WorldMovementCommand>* const outCommands)
    {
        if (outCommands == nullptr)
        {
            return false;
        }

        const TargetTickToCommandBucketMap::iterator found = targetTickToCommands_.find(targetServerTick);
        if (found == targetTickToCommands_.end())
        {
            return false;
        }

        std::vector<WorldMovementCommand> commands;
        commands.reserve(found->second.size());
        for (CommandBucket::value_type& entry : found->second)
        {
            const std::uint32_t commandAgeTicks = targetServerTick - entry.second.admittedServerTick;
            if (commandAgeTicks > metrics_.maximumCommandAgeTicks)
            {
                metrics_.maximumCommandAgeTicks = commandAgeTicks;
            }
            commands.push_back(std::move(entry.second));
        }

        commandCount_ -= commands.size();
        metrics_.takenCommandCount += commands.size();
        *outCommands = std::move(commands);
        targetTickToCommands_.erase(found);
        return true;
    }

    std::size_t WorldMovementCommandStore::RemoveSession(const WorldSessionKey sessionKey) noexcept
    {
        if (!sessionKey.IsValid())
        {
            return 0;
        }

        std::size_t removedCommandCount = 0;
        TargetTickToCommandBucketMap::iterator bucket = targetTickToCommands_.begin();
        while (bucket != targetTickToCommands_.end())
        {
            removedCommandCount += bucket->second.erase(sessionKey);
            if (bucket->second.empty())
            {
                bucket = targetTickToCommands_.erase(bucket);
            }
            else
            {
                ++bucket;
            }
        }

        commandCount_ -= removedCommandCount;
        metrics_.canceledCommandCount += removedCommandCount;
        return removedCommandCount;
    }

    std::size_t WorldMovementCommandStore::Size() const noexcept
    {
        return commandCount_;
    }

    WorldMovementCommandStoreMetrics WorldMovementCommandStore::Metrics() const noexcept
    {
        return metrics_;
    }
} // namespace psnr::world
