#include "pch.h"

#include "WorldCollisionDeathResolver.h"

namespace psnr::world::tests
{
    TEST(WorldCollisionDeathResolverTests, ResolvesStableSimultaneousDeathSetRegardlessOfPairOrder)
    {
        const WorldCollisionProxyKey firstHead{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerHead, 0};
        const WorldCollisionProxyKey secondHead{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerHead, 0};
        const WorldCollisionProxyKey thirdHead{WorldEntityKey{3, 1}, WorldCollisionProxyRole::PlayerHead, 0};
        const WorldCollisionProxyKey thirdBody{WorldEntityKey{3, 1}, WorldCollisionProxyRole::PlayerBody, 4};
        const WorldCollisionProxyKey fourthBody{WorldEntityKey{4, 1}, WorldCollisionProxyRole::PlayerBody, 2};
        const std::vector<WorldCollisionContact> contacts{
            WorldCollisionContact{fourthBody, secondHead},
            WorldCollisionContact{thirdHead, firstHead},
            WorldCollisionContact{secondHead, fourthBody},
            WorldCollisionContact{thirdHead, thirdBody},
        };
        WorldResult<std::vector<WorldEntityKey>> result = WorldCollisionDeathResolver::Resolve(contacts);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<WorldEntityKey> deathSet = result.TakeValue();
        EXPECT_EQ(deathSet,
                  (std::vector<WorldEntityKey>{WorldEntityKey{1, 1}, WorldEntityKey{2, 1}, WorldEntityKey{3, 1}}));
    }

    TEST(WorldCollisionDeathResolverTests, RejectsInvalidContact)
    {
        const WorldCollisionContact bodyBody{
            WorldCollisionProxyKey{WorldEntityKey{1, 1}, WorldCollisionProxyRole::PlayerBody, 0},
            WorldCollisionProxyKey{WorldEntityKey{2, 1}, WorldCollisionProxyRole::PlayerBody, 0},
        };

        const WorldResult<std::vector<WorldEntityKey>> result =
            WorldCollisionDeathResolver::Resolve(std::span<const WorldCollisionContact>{&bodyBody, 1});
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidInput);
    }
} // namespace psnr::world::tests
