#include "pch.h"

#include "WorldSessionRegistry.h"

#include <cassert>
#include <functional>
#include <span>
#include <utility>

namespace psnr::world
{
    const WorldSession* WorldSessionLookup::FindFirstByPlayerId(const std::span<const WorldSession> sessions,
                                                                const std::uint32_t playerId) noexcept
    {
        for (const WorldSession& session : sessions)
        {
            if (session.playerId == playerId)
            {
                return &session;
            }
        }
        return nullptr;
    }

    const WorldSession* WorldSessionLookup::FindUniqueByPlayerId(const std::span<const WorldSession> sessions,
                                                                 const std::uint32_t playerId) noexcept
    {
        const WorldSession* found = nullptr;
        for (const WorldSession& session : sessions)
        {
            if (session.playerId != playerId)
            {
                continue;
            }
            if (found != nullptr)
            {
                return nullptr;
            }
            found = &session;
        }
        return found;
    }

    bool WorldSessionRegistry::TryRegister(const WorldSessionKey sessionKey)
    {
        if (!sessionKey.IsValid() || sessionKeyToDenseIndex_.contains(sessionKey))
        {
            return false;
        }

        const std::size_t denseIndex = sessions_.size();
        sessions_.push_back(WorldSession{sessionKey});
        sessionKeyToDenseIndex_.emplace(sessionKey, denseIndex);
        return true;
    }

    bool WorldSessionRegistry::TryBindPlayer(const WorldSessionKey sessionKey, const std::uint32_t playerId,
                                             const WorldEntityKey entityKey)
    {
        return TryBindPlayer(sessionKey, playerId, entityKey, std::string_view{});
    }

    bool WorldSessionRegistry::TryBindPlayer(const WorldSessionKey sessionKey, const std::uint32_t playerId,
                                             const WorldEntityKey entityKey, const std::string_view displayName)
    {
        if (playerId == 0 || !entityKey.IsValid())
        {
            return false;
        }

        const SessionKeyToDenseIndexMap::iterator found = sessionKeyToDenseIndex_.find(sessionKey);
        if (found == sessionKeyToDenseIndex_.end() || found->second < joinedSessionCount_ ||
            entityKeyToSessionKey_.contains(entityKey))
        {
            return false;
        }

        std::string ownedDisplayName{displayName};
        entityKeyToSessionKey_.emplace(entityKey, sessionKey); // entity key -> session key 매핑

        const std::size_t connectedDenseIndex = found->second;
        const std::size_t joinedDenseIndex = joinedSessionCount_;
        SwapDenseSessions(connectedDenseIndex, joinedDenseIndex);

        WorldSession& session = sessions_[joinedDenseIndex];
        session.playerId = playerId;
        session.entityKey = entityKey;
        session.displayName = std::move(ownedDisplayName);
        ++joinedSessionCount_;
        return true;
    }

    bool WorldSessionRegistry::TryRebindControlledEntity(const WorldSessionKey sessionKey,
                                                         const WorldEntityKey entityKey)
    {
        if (!entityKey.IsValid())
        {
            return false;
        }

        const SessionKeyToDenseIndexMap::const_iterator found = sessionKeyToDenseIndex_.find(sessionKey);
        if (found == sessionKeyToDenseIndex_.end() || found->second >= joinedSessionCount_ ||
            entityKeyToSessionKey_.contains(entityKey))
        {
            return false;
        }

        WorldSession& session = sessions_[found->second];
        entityKeyToSessionKey_.emplace(entityKey, sessionKey);
        entityKeyToSessionKey_.erase(session.entityKey);
        session.entityKey = entityKey;
        return true;
    }

    bool WorldSessionRegistry::TryFind(const WorldSessionKey sessionKey, WorldSession* const outSession) const
    {
        if (outSession == nullptr)
        {
            return false;
        }

        const SessionKeyToDenseIndexMap::const_iterator found = sessionKeyToDenseIndex_.find(sessionKey);
        if (found == sessionKeyToDenseIndex_.end())
        {
            return false;
        }

        *outSession = sessions_[found->second];
        return true;
    }

    std::span<const WorldSession> WorldSessionRegistry::JoinedSessions() const noexcept
    {
        return std::span<const WorldSession>{sessions_.data(), joinedSessionCount_};
    }

    bool WorldSessionRegistry::Remove(const WorldSessionKey sessionKey)
    {
        const SessionKeyToDenseIndexMap::const_iterator found = sessionKeyToDenseIndex_.find(sessionKey);
        if (found == sessionKeyToDenseIndex_.end())
        {
            return false;
        }

        std::size_t removeDenseIndex = found->second;
        if (removeDenseIndex < joinedSessionCount_) // remove joined session
        {
            entityKeyToSessionKey_.erase(sessions_[removeDenseIndex].entityKey);
            const std::size_t lastJoinedDenseIndex = joinedSessionCount_ - 1;
            SwapDenseSessions(removeDenseIndex, lastJoinedDenseIndex); // 가장 뒤에 있는 joined Idx 와 swap
            --joinedSessionCount_;
            removeDenseIndex = joinedSessionCount_;
        }

        RemoveDenseSessionAt(removeDenseIndex);
        return true;
    }

    std::size_t WorldSessionRegistry::Size() const noexcept
    {
        return sessions_.size();
    }

    void WorldSessionRegistry::SwapDenseSessions(const std::size_t leftIndex, const std::size_t rightIndex)
    {
        assert(leftIndex < sessions_.size());
        assert(rightIndex < sessions_.size());
        if (leftIndex == rightIndex)
        {
            return;
        }

        std::swap(sessions_[leftIndex], sessions_[rightIndex]);
        const SessionKeyToDenseIndexMap::iterator leftFound =
            sessionKeyToDenseIndex_.find(sessions_[leftIndex].sessionKey);
        const SessionKeyToDenseIndexMap::iterator rightFound =
            sessionKeyToDenseIndex_.find(sessions_[rightIndex].sessionKey);

        assert(leftFound != sessionKeyToDenseIndex_.end());
        assert(rightFound != sessionKeyToDenseIndex_.end());

        leftFound->second = leftIndex;
        rightFound->second = rightIndex;
    }

    void WorldSessionRegistry::RemoveDenseSessionAt(const std::size_t denseIndex)
    {
        // 가장 뒤에 있는 denseIdx 와 지우려는 대상 Idx 를 스왑하여 dense vector 에서 제거
        assert(denseIndex < sessions_.size());
        const WorldSessionKey removedSessionKey = sessions_[denseIndex].sessionKey;
        const std::size_t lastDenseIndex = sessions_.size() - 1;
        SwapDenseSessions(denseIndex, lastDenseIndex);
        sessionKeyToDenseIndex_.erase(removedSessionKey);
        sessions_.pop_back();
    }

    std::size_t WorldSessionKeyHash::operator()(const WorldSessionKey key) const noexcept
    {
        return std::hash<std::uint64_t>{}(key.value);
    }
} // namespace psnr::world
