#include "pch.h"

#include "WorldMovementCommandStore.h"

#include <algorithm>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldMovementCommand MakeCommand(const std::uint64_t sessionValue, const std::uint32_t playerId,
                                                       const std::uint32_t entityId,
                                                       const std::uint32_t targetServerTick, const float movementInputX)
        {
            return WorldMovementCommand{
                WorldSessionKey{sessionValue},
                playerId,
                WorldEntityKey{entityId, 1},
                targetServerTick,
                targetServerTick,
                movementInputX,
                0.0f,
            };
        }

        [[nodiscard]] bool ContainsCommand(const std::vector<WorldMovementCommand>& commands,
                                           const WorldMovementCommand& expected)
        {
            const std::vector<WorldMovementCommand>::const_iterator found =
                std::find(commands.begin(), commands.end(), expected);
            return found != commands.end();
        }
    } // namespace

    TEST(WorldMovementCommandStoreTests, StoresAndTakesOnlyRequestedTargetTick)
    {
        WorldMovementCommandStore store;
        const WorldMovementCommand tick100 = MakeCommand(10, 7, 3, 100, 0.25f);
        const WorldMovementCommand tick108 = MakeCommand(10, 7, 3, 108, 0.5f);
        std::vector<WorldMovementCommand> commands;

        ASSERT_EQ(store.TryStore(tick100), WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(store.TryStore(tick108), WorldMovementCommandStoreResult::Stored);
        EXPECT_EQ(store.Size(), 2u);

        ASSERT_TRUE(store.TryTake(100, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0], tick100);
        EXPECT_EQ(store.Size(), 1u);

        ASSERT_TRUE(store.TryTake(108, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0], tick108);
        EXPECT_EQ(store.Size(), 0u);
    }

    TEST(WorldMovementCommandStoreTests, AllowsDifferentSessionsAtTheSameTargetTick)
    {
        WorldMovementCommandStore store;
        const WorldMovementCommand first = MakeCommand(10, 7, 3, 100, 0.25f);
        const WorldMovementCommand second = MakeCommand(11, 8, 4, 100, -0.5f);
        std::vector<WorldMovementCommand> commands;

        ASSERT_EQ(store.TryStore(first), WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(store.TryStore(second), WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(store.TryTake(100, &commands));

        ASSERT_EQ(commands.size(), 2u);
        EXPECT_TRUE(ContainsCommand(commands, first));
        EXPECT_TRUE(ContainsCommand(commands, second));
    }

    TEST(WorldMovementCommandStoreTests, RejectsDuplicateSessionTargetTickAndKeepsFirstCommand)
    {
        WorldMovementCommandStore store;
        const WorldMovementCommand first = MakeCommand(10, 7, 3, 100, 0.25f);
        const WorldMovementCommand duplicate = MakeCommand(10, 7, 3, 100, -0.75f);
        std::vector<WorldMovementCommand> commands;

        ASSERT_EQ(store.TryStore(first), WorldMovementCommandStoreResult::Stored);
        EXPECT_EQ(store.TryStore(duplicate), WorldMovementCommandStoreResult::DuplicateTargetTick);
        EXPECT_EQ(store.Size(), 1u);

        ASSERT_TRUE(store.TryTake(100, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0], first);
    }

    TEST(WorldMovementCommandStoreTests, DoubleBufferedReplacesSameSessionEpochWithLatestCommand)
    {
        WorldMovementCommandStore store;
        const WorldMovementCommand first = MakeCommand(10, 7, 3, 100, 0.25f);
        const WorldMovementCommand latest = MakeCommand(10, 7, 3, 100, -0.75f);
        std::vector<WorldMovementCommand> commands;

        ASSERT_EQ(store.TryStore(WorldInboundMode::DoubleBuffered, first), WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(store.TryStore(WorldInboundMode::DoubleBuffered, latest), WorldMovementCommandStoreResult::Replaced);
        EXPECT_EQ(store.Size(), 1u);

        ASSERT_TRUE(store.TryTake(100, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0], latest);

        const WorldMovementCommandStoreMetrics metrics = store.Metrics();
        EXPECT_EQ(metrics.storedCommandCount, 1u);
        EXPECT_EQ(metrics.replacedCommandCount, 1u);
        EXPECT_EQ(metrics.takenCommandCount, 1u);
    }

    TEST(WorldMovementCommandStoreTests, RejectsInvalidCommandsAndPreservesOutputOnFailedTake)
    {
        WorldMovementCommandStore store;
        const WorldMovementCommand invalid;
        const WorldMovementCommand unchanged = MakeCommand(20, 9, 5, 90, 1.0f);
        std::vector<WorldMovementCommand> commands{unchanged};

        EXPECT_EQ(store.TryStore(invalid), WorldMovementCommandStoreResult::InvalidCommand);
        EXPECT_FALSE(store.TryTake(100, &commands));
        ASSERT_EQ(commands.size(), 1u);
        EXPECT_EQ(commands[0], unchanged);

        EXPECT_FALSE(store.TryTake(100, nullptr));
        EXPECT_EQ(store.Size(), 0u);
    }

    TEST(WorldMovementCommandStoreTests, ReportsStoredTakenCanceledAndMaximumCommandAge)
    {
        WorldMovementCommandStore store;
        WorldMovementCommand taken = MakeCommand(10, 7, 3, 108, 0.25f);
        taken.admittedServerTick = 100;
        WorldMovementCommand canceled = MakeCommand(11, 8, 4, 109, -0.5f);
        canceled.admittedServerTick = 105;
        std::vector<WorldMovementCommand> commands;

        ASSERT_EQ(store.TryStore(taken), WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(store.TryStore(canceled), WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(store.TryTake(108, &commands));
        EXPECT_EQ(store.RemoveSession(canceled.sessionKey), 1u);

        const WorldMovementCommandStoreMetrics metrics = store.Metrics();
        EXPECT_EQ(metrics.storedCommandCount, 2u);
        EXPECT_EQ(metrics.replacedCommandCount, 0u);
        EXPECT_EQ(metrics.takenCommandCount, 1u);
        EXPECT_EQ(metrics.canceledCommandCount, 1u);
        EXPECT_EQ(metrics.maximumCommandAgeTicks, 8u);
        EXPECT_EQ(store.Size(), 0u);
    }
} // namespace psnr::world::tests
