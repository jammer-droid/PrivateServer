#include "pch.h"

#include "WorldOverviewPlanner.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        WorldOverviewPlanInput MakeInput()
        {
            WorldOverviewPlanInput input;
            input.serverTick = 100;
            input.overviewId = 7;
            input.mapMinX = -10.0f;
            input.mapMinY = -10.0f;
            input.mapMaxX = 10.0f;
            input.mapMaxY = 10.0f;
            input.activeAreaCenterX = 0.0f;
            input.activeAreaCenterY = 0.0f;
            input.activeAreaRadius = 5.0f;
            return input;
        }

        WorldOverviewPlayerInput MakePlayer(const std::uint32_t playerId, const std::uint32_t growthPoint,
                                            const float tailX, std::string displayName = {})
        {
            return WorldOverviewPlayerInput{
                playerId,
                growthPoint,
                {
                    protocol::v3::WorldOverviewPoint{0.0f, 0.0f},
                    protocol::v3::WorldOverviewPoint{0.4f, 0.0f},
                    protocol::v3::WorldOverviewPoint{tailX, 0.0f},
                },
                std::move(displayName),
            };
        }
    } // namespace

    TEST(WorldOverviewPlannerTests, DownsamplesOrdersAndRanksAlivePlayersDeterministically)
    {
        WorldOverviewPlanInput input = MakeInput();
        input.alivePlayers = {
            MakePlayer(30, 10, 2.4f, "Thirty"),
            MakePlayer(20, 5, 2.4f),
            MakePlayer(10, 10, 2.4f, "Ten"),
        };
        const WorldOverviewPlanner planner;

        WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> result = planner.Plan(input);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<protocol::v3::WorldOverviewSnapshot> chunks = result.TakeValue();
        ASSERT_EQ(chunks.size(), 1u);
        const protocol::v3::WorldOverviewSnapshot& chunk = chunks[0];
        ASSERT_EQ(chunk.players.size(), 3u);
        EXPECT_EQ(chunk.players[0].playerId, 10u);
        EXPECT_EQ(chunk.players[1].playerId, 20u);
        EXPECT_EQ(chunk.players[2].playerId, 30u);
        ASSERT_EQ(chunk.players[0].bodySamples.size(), 4u);
        EXPECT_EQ(chunk.players[0].bodySamples[0], (protocol::v3::WorldOverviewPoint{0.0f, 0.0f}));
        EXPECT_EQ(chunk.players[0].bodySamples[1], (protocol::v3::WorldOverviewPoint{1.0f, 0.0f}));
        EXPECT_EQ(chunk.players[0].bodySamples[2], (protocol::v3::WorldOverviewPoint{2.0f, 0.0f}));
        EXPECT_EQ(chunk.players[0].bodySamples[3], (protocol::v3::WorldOverviewPoint{2.4f, 0.0f}));
        ASSERT_EQ(chunk.leaderboard.size(), 3u);
        EXPECT_EQ(chunk.leaderboard[0], (protocol::v3::WorldOverviewLeaderboardEntry{1, 10, 10, "Ten"}));
        EXPECT_EQ(chunk.leaderboard[1], (protocol::v3::WorldOverviewLeaderboardEntry{1, 30, 10, "Thirty"}));
        EXPECT_EQ(chunk.leaderboard[2], (protocol::v3::WorldOverviewLeaderboardEntry{3, 20, 5, ""}));
    }

    TEST(WorldOverviewPlannerTests, SplitsOnlyBetweenWholePlayerRecords)
    {
        WorldOverviewPlanInput input = MakeInput();
        for (std::uint32_t playerId = 1; playerId <= 3; ++playerId)
        {
            input.alivePlayers.push_back(MakePlayer(playerId, playerId, 500.0f));
        }
        const WorldOverviewPlanner planner;

        WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> result = planner.Plan(input);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<protocol::v3::WorldOverviewSnapshot> chunks = result.TakeValue();
        ASSERT_EQ(chunks.size(), 2u);
        EXPECT_EQ(chunks[0].chunkIndex, 0u);
        EXPECT_EQ(chunks[0].chunkCount, 2u);
        EXPECT_EQ(chunks[0].players.size(), 2u);
        EXPECT_EQ(chunks[0].leaderboard.size(), 3u);
        EXPECT_EQ(chunks[1].chunkIndex, 1u);
        EXPECT_EQ(chunks[1].players.size(), 1u);
        EXPECT_TRUE(chunks[1].leaderboard.empty());
        EXPECT_LE(protocol::v3::WorldOverviewSnapshot::CalculatePayloadBytes(chunks[0]),
                  protocol::v3::WorldOverviewSnapshot::Wire::MaximumPayloadBytes);
        EXPECT_LE(protocol::v3::WorldOverviewSnapshot::CalculatePayloadBytes(chunks[1]),
                  protocol::v3::WorldOverviewSnapshot::Wire::MaximumPayloadBytes);
    }

    TEST(WorldOverviewPlannerTests, RejectsDuplicatePlayerWithoutChangingOutput)
    {
        WorldOverviewPlanInput input = MakeInput();
        input.alivePlayers = {MakePlayer(10, 1, 2.0f), MakePlayer(10, 2, 2.0f)};
        const WorldOverviewPlanner planner;

        const WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> result = planner.Plan(input);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }

    TEST(WorldOverviewPlannerTests, RejectsInvalidDisplayName)
    {
        WorldOverviewPlanInput input = MakeInput();
        input.alivePlayers = {MakePlayer(10, 1, 2.0f, "Player 10")};
        const WorldOverviewPlanner planner;

        const WorldResult<std::vector<protocol::v3::WorldOverviewSnapshot>> result = planner.Plan(input);

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }

    TEST(WorldOverviewPlannerTests, EvaluatesTwoHertzCadenceAndCoalescesCatchUpTicks)
    {
        WorldResult<WorldOverviewCadence> cadenceResult = CreateWorldOverviewCadence(60, 1);
        ASSERT_TRUE(cadenceResult.Succeeded());
        WorldOverviewCadence cadence = cadenceResult.TakeValue();

        WorldOverviewCadenceDecision decision;
        ASSERT_EQ(cadence.Evaluate(1, 29, &decision), WorldOverviewCadenceResult::Evaluated);
        EXPECT_FALSE(decision.IsDue());

        ASSERT_EQ(cadence.Evaluate(30, 30, &decision), WorldOverviewCadenceResult::Evaluated);
        EXPECT_EQ(decision.overviewId, 1u);
        EXPECT_EQ(decision.suppressedOverviewCount, 0u);

        ASSERT_EQ(cadence.Evaluate(31, 90, &decision), WorldOverviewCadenceResult::Evaluated);
        EXPECT_EQ(decision.overviewId, 2u);
        EXPECT_EQ(decision.suppressedOverviewCount, 1u);

        ASSERT_EQ(cadence.Evaluate(91, 120, &decision), WorldOverviewCadenceResult::Evaluated);
        EXPECT_EQ(decision.overviewId, 3u);
        EXPECT_EQ(decision.suppressedOverviewCount, 0u);
    }

    TEST(WorldOverviewPlannerTests, RejectsTickRateThatCannotRepresentTwoHertzWithoutChangingCadence)
    {
        WorldOverviewCadence unchanged;
        WorldOverviewCadenceDecision decision;

        const WorldResult<WorldOverviewCadence> createResult = CreateWorldOverviewCadence(59, 1);
        ASSERT_TRUE(createResult.Failed());
        EXPECT_EQ(createResult.Error(), WorldErrorCode::InvalidConfig);
        EXPECT_EQ(unchanged.Evaluate(1, 1, &decision), WorldOverviewCadenceResult::InvalidInput);
    }
} // namespace psnr::world::tests
