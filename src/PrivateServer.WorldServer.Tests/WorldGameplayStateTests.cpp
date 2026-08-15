#include "pch.h"

#include "WorldGameplayState.h"

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldPhysicsArenaBounds MakeArenaBounds() noexcept
        {
            return WorldPhysicsArenaBounds{-100.0f, -100.0f, 100.0f, 100.0f};
        }

        [[nodiscard]] WorldGameplayConfig MakeValidConfig()
        {
            WorldGameplayConfig config;
            config.minimumPlayersToStart = 2;
            config.scoreToWin = 5;
            config.roundDurationTicks = 1200;
            config.endedDurationTicks = 60;
            config.resourceArchetypeId = 2;
            config.resourceCircleRadius = 0.5f;
            config.resourceScoreValue = 1;
            config.resourceDensityPerUnit2 = 0.000127324f;
            return config;
        }

        [[nodiscard]] WorldGameplayState MakeState()
        {
            WorldResult<WorldGameplayState> result = CreateWorldGameplayState(MakeValidConfig(), MakeArenaBounds());
            EXPECT_TRUE(result.Succeeded());
            return result.TakeValue();
        }
    } // namespace

    TEST(WorldGameplayStateTests, LooksUpCanonicalScoresByPlayerAndEntityIdentity)
    {
        WorldPlayerScore scores[] = {
            WorldPlayerScore{10, WorldEntityKey{1, 1}},
            WorldPlayerScore{20, WorldEntityKey{2, 1}},
        };

        const std::span<const WorldPlayerScore> readOnlyScores = scores;
        EXPECT_EQ(WorldPlayerScoreLookup::FindByPlayerId(readOnlyScores, 20), &scores[1]);
        EXPECT_EQ(WorldPlayerScoreLookup::FindByEntityKey(readOnlyScores, WorldEntityKey{1, 1}), &scores[0]);
        EXPECT_EQ(WorldPlayerScoreLookup::FindByPlayerId(readOnlyScores, 30), nullptr);

        WorldPlayerScore* const mutableScore = WorldPlayerScoreLookup::FindMutableByPlayerId(scores, 10);
        ASSERT_EQ(mutableScore, &scores[0]);
        mutableScore->score = 7;
        EXPECT_EQ(scores[0].score, 7u);
    }

    TEST(WorldGameplayStateTests, CreatesWaitingRoundAndDensityDerivedDormantSlots)
    {
        WorldResult<WorldGameplayState> result = CreateWorldGameplayState(MakeValidConfig(), MakeArenaBounds());
        ASSERT_TRUE(result.Succeeded());
        WorldGameplayState state = result.TakeValue();

        EXPECT_EQ(state.RoundState(), (WorldRoundRuntimeState{1, WorldRoundPhase::Waiting, 0, 0}));
        ASSERT_EQ(state.ResourceSlots().size(), 4u);
        for (std::size_t index = 0; index < state.ResourceSlots().size(); ++index)
        {
            const WorldResourceSlotState& slot = state.ResourceSlots()[index];
            EXPECT_EQ(slot.slotId, index + 1);
            EXPECT_EQ(slot.phase, WorldResourceSlotPhase::Dormant);
        }
        EXPECT_EQ(state.ResourceRegistry().Count(), 0u);
        EXPECT_TRUE(state.PlayerScores().empty());
    }

    TEST(WorldGameplayStateTests, RejectsInvalidConfig)
    {
        WorldGameplayConfig invalidConfig = MakeValidConfig();
        invalidConfig.scoreToWin = 0;

        const WorldResult<WorldGameplayState> result = CreateWorldGameplayState(invalidConfig, MakeArenaBounds());

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidConfig);
    }

    TEST(WorldGameplayStateTests, RegistersPlayersInStablePlayerIdOrder)
    {
        WorldGameplayState state = MakeState();

        EXPECT_EQ(state.TryRegisterPlayer(20, WorldEntityKey{2, 1}), WorldPlayerScoreRegisterResult::Registered);
        EXPECT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::Registered);

        ASSERT_EQ(state.PlayerScores().size(), 2u);
        EXPECT_EQ(state.PlayerScores()[0], (WorldPlayerScore{10, WorldEntityKey{1, 1}, 0}));
        EXPECT_EQ(state.PlayerScores()[1], (WorldPlayerScore{20, WorldEntityKey{2, 1}, 0}));
        EXPECT_EQ(state.PlayerCount(), 2u);
    }

    TEST(WorldGameplayStateTests, RejectsDuplicateAndConflictingPlayerIdentity)
    {
        WorldGameplayState state = MakeState();
        ASSERT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::Registered);

        EXPECT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::DuplicatePlayer);
        EXPECT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{2, 1}), WorldPlayerScoreRegisterResult::IdentityConflict);
        EXPECT_EQ(state.TryRegisterPlayer(20, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::IdentityConflict);
        EXPECT_EQ(state.TryRegisterPlayer(0, WorldEntityKey{3, 1}), WorldPlayerScoreRegisterResult::InvalidArgument);
        EXPECT_EQ(state.TryRegisterPlayer(30, WorldEntityKey{}), WorldPlayerScoreRegisterResult::InvalidArgument);
        EXPECT_EQ(state.PlayerCount(), 1u);
    }

    TEST(WorldGameplayStateTests, RegistersReusedEntitySlotAsNewGenerationLifetime)
    {
        WorldGameplayState state = MakeState();
        ASSERT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::Registered);

        EXPECT_EQ(state.TryRegisterPlayer(20, WorldEntityKey{1, 2}), WorldPlayerScoreRegisterResult::Registered);

        ASSERT_EQ(state.PlayerScores().size(), 2u);
        EXPECT_EQ(state.PlayerScores()[0], (WorldPlayerScore{10, WorldEntityKey{1, 1}, 0}));
        EXPECT_EQ(state.PlayerScores()[1], (WorldPlayerScore{20, WorldEntityKey{1, 2}, 0}));
    }

    TEST(WorldGameplayStateTests, FindsAndRemovesOnlyExactPlayerMapping)
    {
        WorldGameplayState state = MakeState();
        WorldPlayerScore score;
        ASSERT_EQ(state.TryRegisterPlayer(10, WorldEntityKey{1, 1}), WorldPlayerScoreRegisterResult::Registered);

        ASSERT_TRUE(state.TryFindPlayerScore(10, &score));
        EXPECT_EQ(score, (WorldPlayerScore{10, WorldEntityKey{1, 1}, 0}));
        EXPECT_FALSE(state.RemovePlayer(10, WorldEntityKey{1, 2}));
        EXPECT_TRUE(state.RemovePlayer(10, WorldEntityKey{1, 1}));
        EXPECT_FALSE(state.TryFindPlayerScore(10, &score));
        EXPECT_EQ(state.PlayerCount(), 0u);
        EXPECT_EQ(state.Metrics(), WorldGameplayMetrics{});
    }
} // namespace psnr::world::tests
