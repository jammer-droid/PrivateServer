#pragma once

#include "WorldEntityIdentity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace psnr::world
{
    // Runtime session identity를 World 내부 계약으로 변환한 값이다.
    // Runtime의 NrSessionKey 타입을 World domain interface에 직접 노출하지 않는다.
    struct WorldSessionKey final
    {
        std::uint64_t value = 0; // 0 == invalid

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const WorldSessionKey& left,
                                                       const WorldSessionKey& right) noexcept = default;
    };

    // SessionAccepted 직후에는 player binding이 없고, JoinWorld 성공 후 두 값을 함께 채운다.
    struct WorldSession final
    {
        WorldSessionKey sessionKey{}; // Runtime Session Identity for World
        std::uint32_t playerId = 0;

        WorldEntityKey entityKey{}; // JoinWorld 이후 초기화
        std::string displayName;

        [[nodiscard]] bool IsJoined() const noexcept
        {
            return playerId != 0 && entityKey.IsValid();
        }
    };

    class WorldSessionLookup final
    {
    public:
        [[nodiscard]] static const WorldSession* FindFirstByPlayerId(std::span<const WorldSession> sessions,
                                                                     std::uint32_t playerId) noexcept;
        [[nodiscard]] static const WorldSession* FindUniqueByPlayerId(std::span<const WorldSession> sessions,
                                                                      std::uint32_t playerId) noexcept;
    };

    struct WorldSessionKeyHash final
    {
        [[nodiscard]] std::size_t operator()(WorldSessionKey key) const noexcept;
    };

    class WorldSessionRegistry final
    {
    public:
        [[nodiscard]] bool TryRegister(WorldSessionKey sessionKey);
        [[nodiscard]] bool TryBindPlayer(WorldSessionKey sessionKey, std::uint32_t playerId, WorldEntityKey entityKey);
        [[nodiscard]] bool TryBindPlayer(WorldSessionKey sessionKey, std::uint32_t playerId, WorldEntityKey entityKey,
                                         std::string_view displayName);
        [[nodiscard]] bool TryRebindControlledEntity(WorldSessionKey sessionKey, WorldEntityKey entityKey);
        [[nodiscard]] bool TryFind(WorldSessionKey sessionKey, WorldSession* outSession) const;
        [[nodiscard]] std::span<const WorldSession> JoinedSessions() const noexcept;

        bool Remove(WorldSessionKey sessionKey);

        [[nodiscard]] std::size_t Size() const noexcept;

    private:
        using SessionKeyToDenseIndexMap = std::unordered_map<WorldSessionKey, std::size_t, WorldSessionKeyHash>;
        using EntityKeyToSessionKeyMap = std::unordered_map<WorldEntityKey, WorldSessionKey, WorldEntityKeyHash>;

        void SwapDenseSessions(std::size_t leftIndex, std::size_t rightIndex);
        void RemoveDenseSessionAt(std::size_t denseIndex);

        // [0, joinedSessionCount_)는 joined session, 나머지는 connected-only session이다.
        // registry mutation 전까지만 JoinedSessions()가 반환한 span이 유효하다.
        std::vector<WorldSession> sessions_;
        SessionKeyToDenseIndexMap sessionKeyToDenseIndex_; // session key - denseIdx
        EntityKeyToSessionKeyMap entityKeyToSessionKey_;   // entity key - session key
        std::size_t joinedSessionCount_ = 0;
    };
} // namespace psnr::world
