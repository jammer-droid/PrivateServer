#pragma once

#include "WorldMovementCommandStore.h"
#include "WorldMovementTickInput.h"
#include "WorldResult.h"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace psnr::world
{
    // target-tick command와 이전 유효 입력을 현재 tick의 immutable movement input으로 변환한다.
    class WorldMovementTickInputBuilder final
    {
    public:
        [[nodiscard]] WorldResult<std::vector<WorldMovementTickInput>> BuildTickInputs(
            WorldInboundMode inboundMode, std::uint32_t serverTick, std::span<const WorldSession> joinedSessions,
            WorldMovementCommandStore& commandStore) noexcept;

        [[nodiscard]] WorldResult<std::vector<WorldMovementTickInput>> BuildTickInputs(
            std::uint32_t serverTick, std::span<const WorldSession> joinedSessions,
            WorldMovementCommandStore& commandStore) noexcept;

    private:
        struct LastMovementInput final
        {
            std::uint32_t playerId = 0;
            WorldEntityKey entityKey{};
            std::uint32_t lastCommandTick = 0;
            float movementInputX = 0.0f;
            float movementInputY = 0.0f;
        };

        using SessionKeyToLastInputMap = std::unordered_map<WorldSessionKey, LastMovementInput, WorldSessionKeyHash>;

        SessionKeyToLastInputMap sessionKeyToLastInput_; // 각 세션의 가장 최근의 유효한 입력 시의 정보
        std::optional<std::uint32_t> lastBuiltTick_;     // nullopt 는 이전 tick 이 없는 상태
    };
} // namespace psnr::world
