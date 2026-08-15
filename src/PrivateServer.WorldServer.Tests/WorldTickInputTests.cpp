#include "pch.h"

#include "WorldTickInput.h"

#include <span>
#include <utility>
#include <vector>

namespace psnr::world::tests
{
    namespace
    {
        [[nodiscard]] WorldMovementTickInput MakeInput(const std::uint64_t sessionValue,
                                                       const std::uint32_t playerId,
                                                       const std::uint32_t entityId,
                                                       const float movementInputX,
                                                       const float movementInputY)
        {
            return WorldMovementTickInput{
                WorldSessionKey{sessionValue},
                playerId,
                WorldEntityKey{entityId, 1},
                movementInputX,
                movementInputY,
            };
        }
    } // namespace

    TEST(WorldTickInputTests, CreatesImmutableMovementInputView)
    {
        const WorldMovementTickInput first = MakeInput(10, 7, 3, 1.0f, 0.0f);
        const WorldMovementTickInput second = MakeInput(11, 8, 4, 0.0f, -0.5f);
        std::vector<WorldMovementTickInput> inputs{first, second};
        const WorldTickInput tickInput{100, std::move(inputs)};

        EXPECT_EQ(tickInput.ServerTick(), 100u);
        const std::span<const WorldMovementTickInput> movementInputs = tickInput.MovementInputs();
        ASSERT_EQ(movementInputs.size(), 2u);
        EXPECT_EQ(movementInputs[0], first);
        EXPECT_EQ(movementInputs[1], second);
    }

    TEST(WorldTickInputTests, AcceptsTickZeroAndEmptyMovementInputs)
    {
        const WorldTickInput tickInput{0, {}};

        EXPECT_EQ(tickInput.ServerTick(), 0u);
        EXPECT_TRUE(tickInput.MovementInputs().empty());
    }
} // namespace psnr::world::tests
