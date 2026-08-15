#include "pch.h"

#include "WorldSessionRegistry.h"

#include <cstdint>
#include <span>
#include <string>
#include <type_traits>

namespace psnr::world::tests
{
    static_assert(sizeof(WorldSessionKey) == sizeof(std::uint64_t));
    static_assert(std::is_trivially_copyable_v<WorldSessionKey>);

    TEST(WorldSessionRegistryTests, DistinguishesFirstAndUniquePlayerLookupPolicies)
    {
        const WorldSession sessions[] = {
            WorldSession{WorldSessionKey{10}, 7, WorldEntityKey{1, 1}},
            WorldSession{WorldSessionKey{20}, 7, WorldEntityKey{2, 1}},
            WorldSession{WorldSessionKey{30}, 9, WorldEntityKey{3, 1}},
        };

        EXPECT_EQ(WorldSessionLookup::FindFirstByPlayerId(sessions, 7), &sessions[0]);
        EXPECT_EQ(WorldSessionLookup::FindUniqueByPlayerId(sessions, 7), nullptr);
        EXPECT_EQ(WorldSessionLookup::FindUniqueByPlayerId(sessions, 9), &sessions[2]);
        EXPECT_EQ(WorldSessionLookup::FindFirstByPlayerId(sessions, 11), nullptr);
    }

    TEST(WorldSessionRegistryTests, RegistersConnectedSessionWithoutPlayerBinding)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey SessionKey{10};
        WorldSession session{};

        ASSERT_TRUE(registry.TryRegister(SessionKey));
        ASSERT_TRUE(registry.TryFind(SessionKey, &session));
        EXPECT_EQ(session.sessionKey, SessionKey);
        EXPECT_FALSE(session.IsJoined());
        EXPECT_EQ(session.playerId, 0u);
        EXPECT_FALSE(session.entityKey.IsValid());
        EXPECT_EQ(registry.Size(), 1u);
    }

    TEST(WorldSessionRegistryTests, RejectsInvalidDuplicateAndNullOutputOperations)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey InvalidSessionKey{};
        constexpr WorldSessionKey SessionKey{10};
        const WorldSession Unchanged{WorldSessionKey{99}, 7, WorldEntityKey{8, 1}};
        WorldSession output = Unchanged;

        EXPECT_FALSE(registry.TryRegister(InvalidSessionKey));
        ASSERT_TRUE(registry.TryRegister(SessionKey));
        EXPECT_FALSE(registry.TryRegister(SessionKey));
        EXPECT_FALSE(registry.TryFind(SessionKey, nullptr));
        EXPECT_FALSE(registry.TryFind(WorldSessionKey{20}, &output));
        EXPECT_EQ(output.sessionKey, Unchanged.sessionKey);
        EXPECT_EQ(output.playerId, Unchanged.playerId);
        EXPECT_EQ(output.entityKey, Unchanged.entityKey);
        EXPECT_EQ(registry.Size(), 1u);
    }

    TEST(WorldSessionRegistryTests, BindsPlayerAndRejectsDuplicateSessionOrEntity)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey FirstSessionKey{10};
        constexpr WorldSessionKey SecondSessionKey{20};
        constexpr WorldEntityKey EntityKey{100, 1};
        std::string displayName = "Player7";
        WorldSession session{};

        ASSERT_TRUE(registry.TryRegister(FirstSessionKey));
        ASSERT_TRUE(registry.TryRegister(SecondSessionKey));
        EXPECT_FALSE(registry.TryBindPlayer(FirstSessionKey, 0, EntityKey));
        EXPECT_FALSE(registry.TryBindPlayer(FirstSessionKey, 7, WorldEntityKey{}));
        ASSERT_TRUE(registry.TryBindPlayer(FirstSessionKey, 7, EntityKey, displayName));
        displayName.assign("Changed");
        EXPECT_FALSE(registry.TryBindPlayer(FirstSessionKey, 8, WorldEntityKey{200, 1}));
        EXPECT_FALSE(registry.TryBindPlayer(SecondSessionKey, 9, EntityKey));

        ASSERT_TRUE(registry.TryFind(FirstSessionKey, &session));
        EXPECT_TRUE(session.IsJoined());
        EXPECT_EQ(session.playerId, 7u);
        EXPECT_EQ(session.entityKey, EntityKey);
        EXPECT_EQ(session.displayName, "Player7");
    }

    TEST(WorldSessionRegistryTests, RebindsEntityWithoutChangingPlayerBinding)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey FirstSessionKey{10};
        constexpr WorldSessionKey SecondSessionKey{20};
        constexpr WorldEntityKey PreviousEntityKey{100, 1};
        constexpr WorldEntityKey NextEntityKey{100, 2};
        WorldSession session{};

        ASSERT_TRUE(registry.TryRegister(FirstSessionKey));
        ASSERT_TRUE(registry.TryRegister(SecondSessionKey));
        EXPECT_FALSE(registry.TryRebindControlledEntity(FirstSessionKey, NextEntityKey));
        ASSERT_TRUE(registry.TryBindPlayer(FirstSessionKey, 7, PreviousEntityKey));
        EXPECT_FALSE(registry.TryRebindControlledEntity(FirstSessionKey, WorldEntityKey{}));
        ASSERT_TRUE(registry.TryRebindControlledEntity(FirstSessionKey, NextEntityKey));

        ASSERT_TRUE(registry.TryFind(FirstSessionKey, &session));
        EXPECT_EQ(session.playerId, 7u);
        EXPECT_EQ(session.entityKey, NextEntityKey);
        EXPECT_TRUE(registry.TryBindPlayer(SecondSessionKey, 8, PreviousEntityKey));
        EXPECT_FALSE(registry.TryRebindControlledEntity(FirstSessionKey, PreviousEntityKey));
    }

    TEST(WorldSessionRegistryTests, RemovalReleasesEntityBinding)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey FirstSessionKey{10};
        constexpr WorldSessionKey SecondSessionKey{20};
        constexpr WorldEntityKey EntityKey{100, 1};
        WorldSession output{};

        ASSERT_TRUE(registry.TryRegister(FirstSessionKey));
        ASSERT_TRUE(registry.TryRegister(SecondSessionKey));
        ASSERT_TRUE(registry.TryBindPlayer(FirstSessionKey, 7, EntityKey));
        ASSERT_TRUE(registry.Remove(FirstSessionKey));

        EXPECT_FALSE(registry.TryFind(FirstSessionKey, &output));
        EXPECT_FALSE(registry.Remove(FirstSessionKey));
        EXPECT_TRUE(registry.TryBindPlayer(SecondSessionKey, 8, EntityKey, "Player8"));
        ASSERT_TRUE(registry.TryFind(SecondSessionKey, &output));
        EXPECT_EQ(output.displayName, "Player8");
        EXPECT_EQ(registry.Size(), 1u);
    }

    TEST(WorldSessionRegistryTests, ExposesJoinedSessionsAsContiguousPrefix)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey FirstSessionKey{10};
        constexpr WorldSessionKey SecondSessionKey{20};
        constexpr WorldSessionKey ConnectedSessionKey{30};
        constexpr WorldEntityKey FirstEntityKey{100, 1};
        constexpr WorldEntityKey SecondEntityKey{200, 1};

        ASSERT_TRUE(registry.TryRegister(FirstSessionKey));
        ASSERT_TRUE(registry.TryRegister(SecondSessionKey));
        ASSERT_TRUE(registry.TryRegister(ConnectedSessionKey));
        ASSERT_TRUE(registry.TryBindPlayer(SecondSessionKey, 8, SecondEntityKey));
        ASSERT_TRUE(registry.TryBindPlayer(FirstSessionKey, 7, FirstEntityKey));

        const std::span<const WorldSession> joinedSessions = registry.JoinedSessions();
        ASSERT_EQ(joinedSessions.size(), 2u);
        EXPECT_EQ(joinedSessions[0].sessionKey, SecondSessionKey);
        EXPECT_EQ(joinedSessions[0].entityKey, SecondEntityKey);
        EXPECT_EQ(joinedSessions[1].sessionKey, FirstSessionKey);
        EXPECT_EQ(joinedSessions[1].entityKey, FirstEntityKey);
    }

    TEST(WorldSessionRegistryTests, ObserverRemainsOutsideJoinedPlayerPrefix)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey PlayerSessionKey{10};
        constexpr WorldSessionKey ObserverSessionKey{20};
        constexpr WorldEntityKey PlayerEntityKey{100, 1};
        WorldSession observer;

        ASSERT_TRUE(registry.TryRegister(PlayerSessionKey));
        ASSERT_TRUE(registry.TryRegister(ObserverSessionKey));
        ASSERT_TRUE(registry.TryBindObserver(ObserverSessionKey));
        ASSERT_TRUE(registry.TryBindPlayer(PlayerSessionKey, 7, PlayerEntityKey));

        ASSERT_TRUE(registry.TryFind(ObserverSessionKey, &observer));
        EXPECT_TRUE(observer.IsObserver());
        EXPECT_FALSE(observer.IsJoined());
        EXPECT_EQ(observer.playerId, 0u);
        EXPECT_FALSE(observer.entityKey.IsValid());
        EXPECT_FALSE(registry.TryBindObserver(ObserverSessionKey));
        EXPECT_FALSE(registry.TryBindPlayer(ObserverSessionKey, 8, WorldEntityKey{200, 1}));

        const std::span<const WorldSession> joinedSessions = registry.JoinedSessions();
        ASSERT_EQ(joinedSessions.size(), 1u);
        EXPECT_EQ(joinedSessions[0].sessionKey, PlayerSessionKey);
        EXPECT_EQ(registry.RegisteredSessions().size(), 2u);
    }

    TEST(WorldSessionRegistryTests, DenseRemovalKeepsJoinedPrefixAndKeyLookupConsistent)
    {
        WorldSessionRegistry registry;
        constexpr WorldSessionKey FirstJoinedSessionKey{10};
        constexpr WorldSessionKey ConnectedSessionKey{20};
        constexpr WorldSessionKey SecondJoinedSessionKey{30};
        constexpr WorldSessionKey LastConnectedSessionKey{40};
        constexpr WorldEntityKey FirstEntityKey{100, 1};
        constexpr WorldEntityKey SecondEntityKey{200, 1};
        WorldSession found;

        ASSERT_TRUE(registry.TryRegister(FirstJoinedSessionKey));
        ASSERT_TRUE(registry.TryRegister(ConnectedSessionKey));
        ASSERT_TRUE(registry.TryRegister(SecondJoinedSessionKey));
        ASSERT_TRUE(registry.TryRegister(LastConnectedSessionKey));
        ASSERT_TRUE(registry.TryBindPlayer(FirstJoinedSessionKey, 7, FirstEntityKey));
        ASSERT_TRUE(registry.TryBindPlayer(SecondJoinedSessionKey, 8, SecondEntityKey));

        ASSERT_TRUE(registry.Remove(FirstJoinedSessionKey));
        EXPECT_FALSE(registry.TryFind(FirstJoinedSessionKey, &found));
        ASSERT_TRUE(registry.TryFind(SecondJoinedSessionKey, &found));
        EXPECT_EQ(found.entityKey, SecondEntityKey);
        EXPECT_TRUE(registry.TryFind(ConnectedSessionKey, &found));
        EXPECT_TRUE(registry.TryFind(LastConnectedSessionKey, &found));

        const std::span<const WorldSession> joinedSessions = registry.JoinedSessions();
        ASSERT_EQ(joinedSessions.size(), 1u);
        EXPECT_EQ(joinedSessions[0].sessionKey, SecondJoinedSessionKey);

        ASSERT_TRUE(registry.Remove(ConnectedSessionKey));
        EXPECT_FALSE(registry.TryFind(ConnectedSessionKey, &found));
        EXPECT_TRUE(registry.TryFind(LastConnectedSessionKey, &found));
        EXPECT_EQ(registry.Size(), 2u);
    }
} // namespace psnr::world::tests
