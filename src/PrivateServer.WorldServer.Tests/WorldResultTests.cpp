#include "pch.h"

#include "WorldResult.h"

#include <memory>
#include <string>

namespace psnr::world::tests
{
    TEST(WorldResultTests, HoldsSuccessValueWithoutExposingAnError)
    {
        WorldResult<std::unique_ptr<int>> result{std::make_unique<int>(42)};

        ASSERT_TRUE(result.Succeeded());
        EXPECT_FALSE(result.Failed());
        EXPECT_TRUE(result.HasValue());
        EXPECT_EQ(*result.Value(), 42);

        std::unique_ptr<int> value = result.TakeValue();
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, 42);
    }

    TEST(WorldResultTests, HoldsDefaultWorldErrorWithoutExposingAValue)
    {
        const WorldResult<int> result = WorldResult<int>::Failure(WorldErrorCode::InvalidArgument);

        EXPECT_FALSE(result.Succeeded());
        EXPECT_TRUE(result.Failed());
        EXPECT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error(), WorldErrorCode::InvalidArgument);
    }

    TEST(WorldResultTests, SupportsDetailedBoundaryErrorTypes)
    {
        const WorldResult<int, std::string> result =
            WorldResult<int, std::string>::Failure("config path must not be empty");

        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), "config path must not be empty");
    }

    TEST(WorldResultTests, RepresentsOperationsWithoutAValue)
    {
        const WorldResult<void> success = WorldResult<void>::Success();
        const WorldResult<void, std::string> failure = WorldResult<void, std::string>::Failure("write failed");

        EXPECT_TRUE(success.Succeeded());
        EXPECT_TRUE(failure.Failed());
        EXPECT_EQ(failure.Error(), "write failed");
    }
} // namespace psnr::world::tests
