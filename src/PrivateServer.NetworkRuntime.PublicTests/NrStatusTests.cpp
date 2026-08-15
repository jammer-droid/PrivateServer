#include "pch.h"

#include <PrivateServer/NetworkRuntime/NrStatus.h>

namespace psnr::core
{

    namespace
    {
        TEST(NrStatusTests, DefaultConstructedStatusIsSuccess)
        {
            const NrStatus status;

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::Success);
            EXPECT_EQ(status.NativeErrorCode(), 0u);
            EXPECT_TRUE(status.Succeeded());
            EXPECT_FALSE(status.Failed());
        }

        TEST(NrStatusTests, SuccessFactoryReturnsSuccessStatus)
        {
            constexpr NrStatus status = NrStatus::Success();

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::Success);
            EXPECT_EQ(status.NativeErrorCode(), 0u);
            EXPECT_TRUE(status.Succeeded());
            EXPECT_FALSE(status.Failed());
        }

        TEST(NrStatusTests, FailureStatusExposesErrorCodeAndNativeError)
        {
            constexpr NrNativeErrorCode NativeError = 10054u;

            const NrStatus status(NrErrorCode::IoFailed, NativeError);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::IoFailed);
            EXPECT_EQ(status.NativeErrorCode(), NativeError);
            EXPECT_FALSE(status.Succeeded());
            EXPECT_TRUE(status.Failed());
        }

        TEST(NrStatusTests, FailureFactoryExposesErrorCodeAndNativeError)
        {
            constexpr NrNativeErrorCode NativeError = 10054u;

            constexpr NrStatus status = NrStatus::Failure(NrErrorCode::IoFailed, NativeError);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::IoFailed);
            EXPECT_EQ(status.NativeErrorCode(), NativeError);
            EXPECT_FALSE(status.Succeeded());
            EXPECT_TRUE(status.Failed());
        }

        TEST(NrStatusTests, QueueFailureStatusExposesQueueErrorCode)
        {
            const NrStatus fullStatus(NrErrorCode::QueueFull);
            const NrStatus emptyStatus(NrErrorCode::QueueEmpty);

            EXPECT_EQ(fullStatus.ErrorCode(), NrErrorCode::QueueFull);
            EXPECT_EQ(fullStatus.NativeErrorCode(), 0u);
            EXPECT_TRUE(fullStatus.Failed());

            EXPECT_EQ(emptyStatus.ErrorCode(), NrErrorCode::QueueEmpty);
            EXPECT_EQ(emptyStatus.NativeErrorCode(), 0u);
            EXPECT_TRUE(emptyStatus.Failed());
        }

        TEST(NrStatusTests, DispatchFailureStatusExposesDispatchErrorCode)
        {
            const NrStatus status(NrErrorCode::DispatchRuleNotFound);

            EXPECT_EQ(status.ErrorCode(), NrErrorCode::DispatchRuleNotFound);
            EXPECT_EQ(status.NativeErrorCode(), 0u);
            EXPECT_TRUE(status.Failed());
        }
    } // namespace
} // namespace psnr::core
