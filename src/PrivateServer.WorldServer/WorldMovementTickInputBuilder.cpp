#include "pch.h"

#include "WorldMovementTickInputBuilder.h"

#include <new>
#include <utility>

namespace psnr::world
{
    WorldResult<std::vector<WorldMovementTickInput>> WorldMovementTickInputBuilder::BuildTickInputs(
        const WorldInboundMode inboundMode, const std::uint32_t serverTick,
        const std::span<const WorldSession> joinedSessions, WorldMovementCommandStore& commandStore) noexcept
    {
        if (inboundMode != WorldInboundMode::TargetServerTick && inboundMode != WorldInboundMode::DoubleBuffered)
        {
            return WorldResult<std::vector<WorldMovementTickInput>>::Failure(WorldErrorCode::InvalidArgument);
        }
        // DoubleBuffered coordinator는 Ended phase에서 simulation을 멈춰도 absolute server tick은 계속
        // 전진시킨다. 재개 시 forward gap은 허용하되 같은 tick 재처리와 역행은 두 mode 모두 거부한다.
        if (lastBuiltTick_.has_value() &&
            (serverTick <= *lastBuiltTick_ ||
             (inboundMode == WorldInboundMode::TargetServerTick && serverTick != *lastBuiltTick_ + 1)))
        {
            return WorldResult<std::vector<WorldMovementTickInput>>::Failure(WorldErrorCode::NonSequentialTick);
        }

        try
        {
            // sessionKey : session
            using SessionKeyToSessionMap =
                std::unordered_map<WorldSessionKey, const WorldSession*, WorldSessionKeyHash>;
            SessionKeyToSessionMap sessionKeyToSession;
            sessionKeyToSession.reserve(joinedSessions.size());
            for (const WorldSession& session : joinedSessions)
            {
                if (!session.IsJoined())
                {
                    return WorldResult<std::vector<WorldMovementTickInput>>::Failure(WorldErrorCode::InvalidSessionSet);
                }

                // joinedSessions 에서 유효한 session 을 찾아서 추가
                const std::pair<SessionKeyToSessionMap::iterator, bool> inserted =
                    sessionKeyToSession.emplace(session.sessionKey, &session);
                if (!inserted.second)
                {
                    return WorldResult<std::vector<WorldMovementTickInput>>::Failure(WorldErrorCode::InvalidSessionSet);
                }
            }

            std::vector<WorldMovementCommand> commands;
            if (!commandStore.TryTake(serverTick, &commands))
            {
                commands.clear();
            }

            // command.sessionKey : command
            using SessionKeyToCommandMap =
                std::unordered_map<WorldSessionKey, WorldMovementCommand, WorldSessionKeyHash>;
            SessionKeyToCommandMap sessionKeyToCommand;
            sessionKeyToCommand.reserve(commands.size());
            for (WorldMovementCommand& command : commands)
            {
                sessionKeyToCommand.emplace(command.sessionKey, std::move(command));
            }

            std::vector<WorldMovementTickInput> inputs;
            inputs.reserve(joinedSessions.size());

            if (inboundMode == WorldInboundMode::DoubleBuffered)
            {
                for (const WorldSession& session : joinedSessions)
                {
                    const SessionKeyToCommandMap::const_iterator commandFound =
                        sessionKeyToCommand.find(session.sessionKey);
                    if (commandFound == sessionKeyToCommand.end() ||
                        commandFound->second.playerId != session.playerId ||
                        commandFound->second.entityKey != session.entityKey)
                    {
                        continue;
                    }

                    inputs.push_back(WorldMovementTickInput{
                        session.sessionKey,
                        session.playerId,
                        session.entityKey,
                        commandFound->second.movementInputX,
                        commandFound->second.movementInputY,
                    });
                }

                sessionKeyToLastInput_.clear();
                lastBuiltTick_ = serverTick;
                return WorldResult<std::vector<WorldMovementTickInput>>(std::move(inputs));
            }

            SessionKeyToLastInputMap nextLastInputs; // 각 세션의 가장 최근 유효 입력 갱신을 위한 map
            nextLastInputs.reserve(joinedSessions.size());

            for (const WorldSession& session : joinedSessions)
            {
                WorldMovementTickInput input{
                    session.sessionKey, session.playerId, session.entityKey, 0.0f, 0.0f,
                };

                // WorldMovementCommand 에 있는 클라이언트 정보와 joinedSessions 의 session 정보 대조
                // 현재 session/entity에 적용할 유효한 신규 command가 있음
                const SessionKeyToCommandMap::const_iterator commandFound =
                    sessionKeyToCommand.find(session.sessionKey);
                if (commandFound != sessionKeyToCommand.end() && commandFound->second.playerId == session.playerId &&
                    commandFound->second.entityKey == session.entityKey)
                {
                    input.movementInputX = commandFound->second.movementInputX;
                    input.movementInputY = commandFound->second.movementInputY;
                    nextLastInputs.emplace(session.sessionKey, LastMovementInput{
                                                                   session.playerId,
                                                                   session.entityKey,
                                                                   serverTick,
                                                                   input.movementInputX,
                                                                   input.movementInputY,
                                                               });
                }
                else // 현재 session/entity에 적용할 유효한 신규 command가 없음
                {
                    const SessionKeyToLastInputMap::const_iterator lastInputFound =
                        sessionKeyToLastInput_.find(session.sessionKey);
                    if (lastInputFound != sessionKeyToLastInput_.end() &&
                        lastInputFound->second.playerId == session.playerId &&
                        lastInputFound->second.entityKey == session.entityKey &&
                        serverTick - lastInputFound->second.lastCommandTick <= MaxInputHoldTicks)
                    { // 기존 값으로 덮음, MaxInputHoldTicks 지나면 (0, 0) 으로 들어감
                        input.movementInputX = lastInputFound->second.movementInputX;
                        input.movementInputY = lastInputFound->second.movementInputY;
                        nextLastInputs.emplace(session.sessionKey, lastInputFound->second);
                    }
                }

                inputs.push_back(input);
            }

            // 현재 movement tick input은 entity마다 하나이고 각 input은 자기 entity의 movement 값만 갱신한다.
            // 따라서 처리 순서가 최종 결과를 바꾸지 않으므로 매 tick 별도 정렬을 하지 않는다.
            // 이후 같은 entity를 여러 command가 변경하거나 shared 값 누적, entity 생성/제거, 충돌 결과 commit처럼
            // 순서가 결과에 영향을 주는 처리가 추가되면 최종 WorldTickInput 또는 commit 경계에서 정렬한다.

            sessionKeyToLastInput_ = std::move(nextLastInputs);
            lastBuiltTick_ = serverTick;
            return WorldResult<std::vector<WorldMovementTickInput>>(std::move(inputs));
        }
        catch (const std::bad_alloc&)
        {
            return WorldResult<std::vector<WorldMovementTickInput>>::Failure(WorldErrorCode::AllocationFailed);
        }
    }

    WorldResult<std::vector<WorldMovementTickInput>> WorldMovementTickInputBuilder::BuildTickInputs(
        const std::uint32_t serverTick, const std::span<const WorldSession> joinedSessions,
        WorldMovementCommandStore& commandStore) noexcept
    {
        return BuildTickInputs(WorldInboundMode::TargetServerTick, serverTick, joinedSessions, commandStore);
    }
} // namespace psnr::world
