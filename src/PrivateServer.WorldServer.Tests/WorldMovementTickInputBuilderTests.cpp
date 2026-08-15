#include "pch.h"

#include "WorldMovementTickInputBuilder.h"

#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldSession MakeSession(const std::uint64_t sessionValue, const std::uint32_t playerId,
                                               const std::uint32_t entityId, const std::uint32_t entityGeneration = 1)
        {
            return WorldSession{
                WorldSessionKey{sessionValue},
                playerId,
                WorldEntityKey{entityId, entityGeneration},
            };
        }

        [[nodiscard]] WorldMovementCommand MakeCommand(const WorldSession& session,
                                                       const std::uint32_t targetServerTick, const float movementInputX,
                                                       const float movementInputY)
        {
            return WorldMovementCommand{
                session.sessionKey, session.playerId, session.entityKey, targetServerTick,
                targetServerTick,   movementInputX,   movementInputY,
            };
        }

        [[nodiscard]] const WorldMovementTickInput* FindInput(const std::vector<WorldMovementTickInput>& inputs,
                                                              const WorldEntityKey entityKey)
        {
            for (const WorldMovementTickInput& input : inputs)
            {
                if (input.entityKey == entityKey)
                {
                    return &input;
                }
            }

            return nullptr;
        }

        [[nodiscard]] WorldResult<void> BuildAndStore(WorldMovementTickInputBuilder& builder,
                                                      const WorldInboundMode inboundMode,
                                                      const std::uint32_t serverTick,
                                                      const std::span<const WorldSession> sessions,
                                                      WorldMovementCommandStore& store,
                                                      std::vector<WorldMovementTickInput>* const outInputs)
        {
            WorldResult<std::vector<WorldMovementTickInput>> result =
                builder.BuildTickInputs(inboundMode, serverTick, sessions, store);
            if (result.Failed())
            {
                return WorldResult<void>::Failure(result.Error());
            }
            *outInputs = result.TakeValue();
            return WorldResult<void>::Success();
        }

        [[nodiscard]] WorldResult<void> BuildAndStore(WorldMovementTickInputBuilder& builder,
                                                      const std::uint32_t serverTick,
                                                      const std::span<const WorldSession> sessions,
                                                      WorldMovementCommandStore& store,
                                                      std::vector<WorldMovementTickInput>* const outInputs)
        {
            return BuildAndStore(builder, WorldInboundMode::TargetServerTick, serverTick, sessions, store, outInputs);
        }
    } // namespace

    TEST(WorldMovementTickInputBuilderTests, BuildsCurrentCommandsForEachEntity)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession second = MakeSession(11, 8, 4);
        const WorldSession first = MakeSession(10, 7, 3);
        const std::vector<WorldSession> sessions{second, first};
        std::vector<WorldMovementTickInput> inputs;

        ASSERT_EQ(store.TryStore(MakeCommand(first, 100, 0.25f, 0.5f)), WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(store.TryStore(MakeCommand(second, 100, -0.5f, 0.0f)), WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(BuildAndStore(builder, 100, sessions, store, &inputs).Succeeded());

        ASSERT_EQ(inputs.size(), 2u);
        const WorldMovementTickInput* const firstInput = FindInput(inputs, first.entityKey);
        const WorldMovementTickInput* const secondInput = FindInput(inputs, second.entityKey);
        ASSERT_NE(firstInput, nullptr);
        ASSERT_NE(secondInput, nullptr);
        EXPECT_EQ(firstInput->movementInputX, 0.25f);
        EXPECT_EQ(firstInput->movementInputY, 0.5f);
        EXPECT_EQ(secondInput->movementInputX, -0.5f);
        EXPECT_EQ(secondInput->movementInputY, 0.0f);
        EXPECT_EQ(store.Size(), 0u);
    }

    TEST(WorldMovementTickInputBuilderTests, HoldsLastInputForTwoTicksThenUsesNeutralInput)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession session = MakeSession(10, 7, 3);
        const std::vector<WorldSession> sessions{session};
        std::vector<WorldMovementTickInput> inputs;

        ASSERT_EQ(store.TryStore(MakeCommand(session, 100, 1.0f, -0.5f)), WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(BuildAndStore(builder, 100, sessions, store, &inputs).Succeeded());
        EXPECT_EQ(inputs[0].movementInputX, 1.0f);
        EXPECT_EQ(inputs[0].movementInputY, -0.5f);

        ASSERT_TRUE(BuildAndStore(builder, 101, sessions, store, &inputs).Succeeded());
        EXPECT_EQ(inputs[0].movementInputX, 1.0f);
        EXPECT_EQ(inputs[0].movementInputY, -0.5f);

        ASSERT_TRUE(BuildAndStore(builder, 102, sessions, store, &inputs).Succeeded());
        EXPECT_EQ(inputs[0].movementInputX, 1.0f);
        EXPECT_EQ(inputs[0].movementInputY, -0.5f);

        ASSERT_TRUE(BuildAndStore(builder, 103, sessions, store, &inputs).Succeeded());
        EXPECT_EQ(inputs[0].movementInputX, 0.0f);
        EXPECT_EQ(inputs[0].movementInputY, 0.0f);
    }

    TEST(WorldMovementTickInputBuilderTests, UsesNeutralInputAfterControlledEntityRebind)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession oldSession = MakeSession(10, 7, 3, 1);
        const WorldSession reboundSession = MakeSession(10, 7, 4, 1);
        const std::vector<WorldSession> oldSessions{oldSession};
        const std::vector<WorldSession> reboundSessions{reboundSession};
        std::vector<WorldMovementTickInput> inputs;

        ASSERT_EQ(store.TryStore(MakeCommand(oldSession, 100, 1.0f, 0.0f)), WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(BuildAndStore(builder, 100, oldSessions, store, &inputs).Succeeded());
        ASSERT_TRUE(BuildAndStore(builder, 101, reboundSessions, store, &inputs).Succeeded());

        ASSERT_EQ(inputs.size(), 1u);
        EXPECT_EQ(inputs[0].entityKey, reboundSession.entityKey);
        EXPECT_EQ(inputs[0].movementInputX, 0.0f);
        EXPECT_EQ(inputs[0].movementInputY, 0.0f);
    }

    TEST(WorldMovementTickInputBuilderTests, DoubleBufferedBuildsOnlySessionsWithCurrentEpochInput)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession first = MakeSession(10, 7, 3);
        const WorldSession second = MakeSession(11, 8, 4);
        const std::vector<WorldSession> sessions{first, second};
        std::vector<WorldMovementTickInput> inputs;

        ASSERT_EQ(store.TryStore(WorldInboundMode::DoubleBuffered, MakeCommand(first, 100, 0.25f, 0.5f)),
                  WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(
            BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 100, sessions, store, &inputs).Succeeded());

        ASSERT_EQ(inputs.size(), 1u);
        EXPECT_EQ(inputs[0].sessionKey, first.sessionKey);
        EXPECT_EQ(inputs[0].entityKey, first.entityKey);
        EXPECT_EQ(inputs[0].movementInputX, 0.25f);
        EXPECT_EQ(inputs[0].movementInputY, 0.5f);

        ASSERT_TRUE(
            BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 101, sessions, store, &inputs).Succeeded());
        EXPECT_TRUE(inputs.empty());

        ASSERT_EQ(store.TryStore(WorldInboundMode::DoubleBuffered, MakeCommand(second, 102, -0.75f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(
            BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 102, sessions, store, &inputs).Succeeded());
        ASSERT_EQ(inputs.size(), 1u);
        EXPECT_EQ(inputs[0].sessionKey, second.sessionKey);
        EXPECT_EQ(inputs[0].movementInputX, -0.75f);
    }

    TEST(WorldMovementTickInputBuilderTests, DoubleBufferedUsesLatestValidCommandForSession)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession session = MakeSession(10, 7, 3);
        const std::vector<WorldSession> sessions{session};
        std::vector<WorldMovementTickInput> inputs;

        ASSERT_EQ(store.TryStore(WorldInboundMode::DoubleBuffered, MakeCommand(session, 100, 0.25f, 0.0f)),
                  WorldMovementCommandStoreResult::Stored);
        ASSERT_EQ(store.TryStore(WorldInboundMode::DoubleBuffered, MakeCommand(session, 100, -0.5f, 0.75f)),
                  WorldMovementCommandStoreResult::Replaced);

        ASSERT_TRUE(
            BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 100, sessions, store, &inputs).Succeeded());
        ASSERT_EQ(inputs.size(), 1u);
        EXPECT_EQ(inputs[0].movementInputX, -0.5f);
        EXPECT_EQ(inputs[0].movementInputY, 0.75f);
    }

    TEST(WorldMovementTickInputBuilderTests, DoubleBufferedAllowsForwardGapAfterSimulationPause)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession session = MakeSession(10, 7, 3);
        const std::vector<WorldSession> sessions{session};
        std::vector<WorldMovementTickInput> inputs;

        ASSERT_TRUE(
            BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 100, sessions, store, &inputs).Succeeded());
        ASSERT_EQ(inputs.size(), 0u);

        EXPECT_TRUE(BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 102, {}, store, &inputs).Succeeded());
        EXPECT_TRUE(inputs.empty());
        const WorldResult<void> repeatedResult =
            BuildAndStore(builder, WorldInboundMode::DoubleBuffered, 102, {}, store, &inputs);
        ASSERT_TRUE(repeatedResult.Failed());
        EXPECT_EQ(repeatedResult.Error(), WorldErrorCode::NonSequentialTick);
    }

    TEST(WorldMovementTickInputBuilderTests, RejectsNonSequentialTickWithoutConsumingCommandsOrChangingOutput)
    {
        WorldMovementCommandStore store;
        WorldMovementTickInputBuilder builder;
        const WorldSession session = MakeSession(10, 7, 3);
        const std::vector<WorldSession> sessions{session};
        const WorldMovementTickInput unchanged{
            session.sessionKey, session.playerId, session.entityKey, -1.0f, -1.0f,
        };
        std::vector<WorldMovementTickInput> inputs{unchanged};

        ASSERT_EQ(store.TryStore(MakeCommand(session, 100, 0.25f, 0.5f)), WorldMovementCommandStoreResult::Stored);
        ASSERT_TRUE(BuildAndStore(builder, 100, sessions, store, &inputs).Succeeded());
        ASSERT_EQ(store.TryStore(MakeCommand(session, 102, 0.75f, 0.0f)), WorldMovementCommandStoreResult::Stored);

        inputs = {unchanged};
        const WorldResult<void> skippedTickResult = BuildAndStore(builder, 102, sessions, store, &inputs);
        ASSERT_TRUE(skippedTickResult.Failed());
        EXPECT_EQ(skippedTickResult.Error(), WorldErrorCode::NonSequentialTick);
        ASSERT_EQ(inputs.size(), 1u);
        EXPECT_EQ(inputs[0], unchanged);
        EXPECT_EQ(store.Size(), 1u);

        ASSERT_TRUE(BuildAndStore(builder, 101, sessions, store, &inputs).Succeeded());
        ASSERT_TRUE(BuildAndStore(builder, 102, sessions, store, &inputs).Succeeded());
        EXPECT_EQ(inputs[0].movementInputX, 0.75f);
        EXPECT_EQ(store.Size(), 0u);
    }
} // namespace psnr::world::tests
