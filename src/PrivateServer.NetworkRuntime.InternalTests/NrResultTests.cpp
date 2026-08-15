#include "pch.h"

#include "NrResult.h"

#include <memory>

namespace psnr::core
{

    namespace
    {
        TEST(NrResultTests, SuccessResultStoresValueAndStatus)
        {
            NrResult<int> result(42);

            EXPECT_TRUE(result.Succeeded());
            EXPECT_FALSE(result.Failed());
            EXPECT_TRUE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::Success);
            EXPECT_EQ(result.NativeErrorCode(), 0u);
            EXPECT_EQ(result.Value(), 42);
        }

        TEST(NrResultTests, FailureResultStoresStatusWithoutValue)
        {
            constexpr NrNativeErrorCode NativeError = 10054u;

            NrResult<int> result = NrResult<int>::Failure(NrErrorCode::IoFailed, NativeError);

            EXPECT_FALSE(result.Succeeded());
            EXPECT_TRUE(result.Failed());
            EXPECT_FALSE(result.HasValue());
            EXPECT_EQ(result.ErrorCode(), NrErrorCode::IoFailed);
            EXPECT_EQ(result.NativeErrorCode(), NativeError);
        }

        TEST(NrResultTests, TakeValueMovesStoredValue)
        {
            auto value = std::make_unique<int>(7);
            NrResult<std::unique_ptr<int>> result(std::move(value));

            std::unique_ptr<int> taken = result.TakeValue();

            ASSERT_NE(taken, nullptr);
            EXPECT_EQ(*taken, 7);
        }
    } // namespace
} // namespace psnr::core
